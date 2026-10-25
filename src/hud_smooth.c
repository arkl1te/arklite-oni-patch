/* Smooth, sharp HUD pictures at any HUD scale (hud_scale.c).
 *
 * The HUD's pictures are small textures (the rings are 128 px) drawn one texel per pixel at
 * 100%. Scaled, OpenGL stretches them with GL_LINEAR, which looks blurred; GL_NEAREST looks
 * blocky. Instead, the first time a HUD texture is drawn at a scale, we read it back from GL,
 * resample it on the CPU to its size on screen with a Lanczos-3 filter (the sharpest common
 * filter that keeps edges smooth; alpha premultiplied so edges don't fringe), and upload that
 * as a texture of our own. While the HUD draws the primitive, the TXMP's GL name (+0x9c) is
 * swapped for ours, so the driver binds ours, and the sprites are put on whole pixels so the
 * copy lands one texel per pixel.
 *
 * The GL driver (0x40d540) binds a texture's name +0x9c on unit 0 unless it is the texture it
 * bound last (0x560614, compared only); we set that to a value no texture has whenever we bind
 * something, so it binds again. It re-uploads a texture whose +0xa0 byte is set, deleting the
 * name +0x9c first, so such a texture is never swapped. A copy is made again when the texture's
 * GL name, size or flags change; all are dropped when the level or the GL context changes.
 * The GL functions come from opengl32.dll, which Oni uses; glActiveTextureARB and
 * glClientActiveTextureARB from Oni's table (*0x560604 +0x568, +0x56c), used when the driver
 * multitextures (*0x560600 +0x584), as the driver does before it binds on unit 0. */
#include "arklite.h"

__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);

#define M3gDrawState     (*(u8 ***)0x6b8cb0)   /* [4] the texture, as committed for the draw */
#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define GLgFunctions (*(u8 **)0x560604)
#define GLgDriver    (*(u8 **)0x560600)
#define GLgBound     (*(u8 **)0x560614)
#define cNotBound    ((u8 *)0xffffffff)

/* TXMP */
#define TXMP_FLAGS(t)  (*(u32 *)((t) + 0x80))   /* 0x4 no U wrap, 0x8 no V wrap */
#define TXMP_WIDTH(t)  (*(u16 *)((t) + 0x84))
#define TXMP_HEIGHT(t) (*(u16 *)((t) + 0x86))
#define TXMP_GLNAME(t) (*(u32 *)((t) + 0x9c))
#define TXMP_DIRTY(t)  (*(u8 *)((t) + 0xa0))

#define GL_TEXTURE_2D         0x0de1
#define GL_TEXTURE_WIDTH      0x1000
#define GL_TEXTURE_HEIGHT     0x1001
#define GL_UNSIGNED_BYTE      0x1401
#define GL_RGBA               0x1908
#define GL_RGBA8              0x8058
#define GL_LINEAR             0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_REPEAT             0x2901
#define GL_CLAMP_TO_EDGE      0x812f
#define GL_TEXTURE0           0x84c0
#define GL_NO_ERROR           0

#define cMaxSide 4096
#define cLobes   3

static struct {
	int  loaded, ok;
	void (WINAPI *BindTexture)(u32, u32);
	void (WINAPI *TexParameteri)(u32, u32, int);
	void (WINAPI *GenTextures)(int, u32 *);
	void (WINAPI *DeleteTextures)(int, const u32 *);
	void (WINAPI *TexImage2D)(u32, int, int, int, int, int, u32, u32, const void *);
	void (WINAPI *GetTexImage)(u32, int, u32, u32, void *);
	void (WINAPI *GetTexLevelParameteriv)(u32, int, u32, int *);
	u32  (WINAPI *GetError)(void);
	void (WINAPI *PixelStorei)(u32, int);
	void (WINAPI *GetIntegerv)(u32, int *);
	void *(WINAPI *GetCurrentContext)(void);
} gl;

typedef struct {
	u8   *texture;      /* compared only: it may be gone */
	u32  name, flags;   /* the texture's GL name and flags when the copy was made */
	u16  width, height;
	float scale;
	u32  copy;          /* our GL name, 0 if the copy failed */
} Copy;

static Copy  g_copies[48];
static int   g_copyCount, g_nextEvict;
static void *g_context;
static int   g_level = -1;

static int load_gl(void)
{
	HANDLE m;

	if (gl.loaded) return gl.ok;
	gl.loaded = 1;
	if (!(m = GetModuleHandleA("opengl32.dll"))) return 0;
	gl.BindTexture = GetProcAddress(m, "glBindTexture");
	gl.TexParameteri = GetProcAddress(m, "glTexParameteri");
	gl.GenTextures = GetProcAddress(m, "glGenTextures");
	gl.DeleteTextures = GetProcAddress(m, "glDeleteTextures");
	gl.TexImage2D = GetProcAddress(m, "glTexImage2D");
	gl.GetTexImage = GetProcAddress(m, "glGetTexImage");
	gl.GetTexLevelParameteriv = GetProcAddress(m, "glGetTexLevelParameteriv");
	gl.GetError = GetProcAddress(m, "glGetError");
	gl.PixelStorei = GetProcAddress(m, "glPixelStorei");
	gl.GetIntegerv = GetProcAddress(m, "glGetIntegerv");
	gl.GetCurrentContext = GetProcAddress(m, "wglGetCurrentContext");
	gl.ok = gl.BindTexture && gl.TexParameteri && gl.GenTextures && gl.DeleteTextures && gl.TexImage2D &&
	        gl.GetTexImage && gl.GetTexLevelParameteriv && gl.GetError && gl.PixelStorei &&
	        gl.GetIntegerv && gl.GetCurrentContext;
	log_msg("hud smooth: %s", gl.ok ? "ready" : "OpenGL functions missing, HUD pictures stay as they are");
	return gl.ok;
}

/* Binds a GL name on unit 0 the way the driver does, and makes the driver bind again after */
static void bind_unit0(u32 name)
{
	u8 *table = GLgFunctions, *driver = GLgDriver;

	if (table && driver && *(u32 *)(driver + 0x584)) {
		(*(void (WINAPI **)(u32))(table + 0x568))(GL_TEXTURE0);   /* glActiveTextureARB */
		(*(void (WINAPI **)(u32))(table + 0x56c))(GL_TEXTURE0);   /* glClientActiveTextureARB */
	}
	gl.BindTexture(GL_TEXTURE_2D, name);
	GLgBound = cNotBound;
}

/* Oni sets GL_UNPACK_ROW_LENGTH and GL_UNPACK_ALIGNMENT for its 2D uploads (0x48882b, 0x489671)
 * and leaves them; ours read and write whole tightly packed rows, so the pixel store goes to
 * GL's defaults around them and back after. */
static const u32 g_pixelStore[] = {
	0x0cf2, 0x0cf3, 0x0cf4, 0x0cf5,   /* GL_UNPACK_ROW_LENGTH, _SKIP_ROWS, _SKIP_PIXELS, _ALIGNMENT */
	0x0d02, 0x0d03, 0x0d04, 0x0d05,   /* GL_PACK_... */
};
static int g_pixelStoreSaved[8];

static void pixel_store_default(void)
{
	for (int i = 0; i < 8; i++) {
		g_pixelStoreSaved[i] = (i & 3) == 3 ? 4 : 0;
		gl.GetIntegerv(g_pixelStore[i], &g_pixelStoreSaved[i]);
		gl.PixelStorei(g_pixelStore[i], (i & 3) == 3 ? 4 : 0);
	}
}

static void pixel_store_restore(void)
{
	for (int i = 0; i < 8; i++) gl.PixelStorei(g_pixelStore[i], g_pixelStoreSaved[i]);
}

/* ---- Lanczos-3 ------------------------------------------------------------------------------- */

static int floor_i(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }

/* sin(pi x) */
static float sin_pi(float x)
{
	float y, y2;

	x -= 2.0f * (float)(int)(x * 0.5f);   /* (-2, 2) */
	if (x > 1.0f) x -= 2.0f;
	else if (x < -1.0f) x += 2.0f;        /* [-1, 1] */
	if (x > 0.5f) x = 1.0f - x;
	else if (x < -0.5f) x = -1.0f - x;    /* [-0.5, 0.5] */
	y = x * 3.14159265f;
	y2 = y * y;
	return y * (1.0f - y2 / 6.0f * (1.0f - y2 / 20.0f * (1.0f - y2 / 42.0f * (1.0f - y2 / 72.0f * (1.0f - y2 / 110.0f)))));
}

static float lanczos(float t)
{
	if (t < 0.0f) t = -t;
	if (t < 1e-5f) return 1.0f;
	if (t >= (float)cLobes) return 0.0f;
	return (float)cLobes * sin_pi(t) * sin_pi(t / cLobes) / (9.8696044f * t * t);
}

static int wrap_index(int i, int n, int clamp)
{
	if (clamp) return i < 0 ? 0 : i >= n ? n - 1 : i;
	i %= n;
	return i < 0 ? i + n : i;
}

/* Source pixels one output pixel reads: all within the filter's reach of its center */
static int axis_tap_count(int in, int out)
{
	float stretch = out < in ? (float)in / out : 1.0f;
	return 2 * (int)(cLobes * stretch) + 4;
}

/* For each of out outputs from in inputs: taps source indexes and weights (normalized) */
static int axis_taps(int in, int out, int clamp, int *index, float *weight)
{
	float scale = (float)out / in, stretch = scale < 1.0f ? 1.0f / scale : 1.0f;
	float support = cLobes * stretch;
	int taps = axis_tap_count(in, out);

	for (int o = 0; o < out; o++) {
		float center = (o + 0.5f) / scale - 0.5f, sum = 0.0f;
		int first = floor_i(center - support) + 1;

		for (int k = 0; k < taps; k++) {
			float w = lanczos((first + k - center) / stretch);
			index[o * taps + k] = wrap_index(first + k, in, clamp);
			weight[o * taps + k] = w;
			sum += w;
		}
		if (sum != 0.0f)
			for (int k = 0; k < taps; k++) weight[o * taps + k] /= sum;
	}
	return taps;
}

static u8 to_byte(float v)
{
	v = v * 255.0f + 0.5f;
	return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8)(int)v;
}

/* RGBA bytes sw x sh -> dw x dh, alpha premultiplied while filtering */
static int resample(const u8 *src, int sw, int sh, u8 *dst, int dw, int dh, int clampU, int clampV)
{
	int tapsX = axis_tap_count(sw, dw), tapsY = axis_tap_count(sh, dh);
	int maxTaps = tapsX > tapsY ? tapsX : tapsY;
	u32 bytes = (u32)(dw + dh) * maxTaps * 8 + (u32)dw * sh * 16;
	u8 *work = VirtualAlloc(0, bytes, 0x3000, 4);
	int *ix, *iy, tx, ty;
	float *wx, *wy, *row;

	if (!work) return 0;
	ix = (int *)work;
	wx = (float *)(ix + dw * maxTaps);
	iy = (int *)(wx + dw * maxTaps);
	wy = (float *)(iy + dh * maxTaps);
	row = wy + dh * maxTaps;   /* dw x sh premultiplied RGBA */
	tx = axis_taps(sw, dw, clampU, ix, wx);
	ty = axis_taps(sh, dh, clampV, iy, wy);

	for (int y = 0; y < sh; y++) {
		const u8 *line = src + (u32)y * sw * 4;
		for (int x = 0; x < dw; x++) {
			float r = 0, g = 0, b = 0, a = 0;
			for (int k = 0; k < tx; k++) {
				const u8 *p = line + ix[x * tx + k] * 4;
				float w = wx[x * tx + k] * (p[3] * (1.0f / 255.0f));
				r += w * p[0]; g += w * p[1]; b += w * p[2];
				a += wx[x * tx + k] * p[3];
			}
			float *o = row + ((u32)y * dw + x) * 4;
			o[0] = r * (1.0f / 255.0f); o[1] = g * (1.0f / 255.0f); o[2] = b * (1.0f / 255.0f); o[3] = a * (1.0f / 255.0f);
		}
	}
	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			float r = 0, g = 0, b = 0, a = 0;
			u8 *o = dst + ((u32)y * dw + x) * 4;
			for (int k = 0; k < ty; k++) {
				const float *p = row + ((u32)iy[y * ty + k] * dw + x) * 4;
				float w = wy[y * ty + k];
				r += w * p[0]; g += w * p[1]; b += w * p[2]; a += w * p[3];
			}
			a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
			if (a <= 0.0f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
			r = r < 0.0f ? 0.0f : r > a ? a : r;
			g = g < 0.0f ? 0.0f : g > a ? a : g;
			b = b < 0.0f ? 0.0f : b > a ? a : b;
			o[0] = to_byte(r / a); o[1] = to_byte(g / a); o[2] = to_byte(b / a); o[3] = to_byte(a);
		}
	}
	VirtualFree(work, 0, 0x8000);
	return 1;
}

/* ---- Copies --------------------------------------------------------------------------------- */

static void drop(Copy *c, int deleteCopy)
{
	if (deleteCopy && c->copy) gl.DeleteTextures(1, &c->copy);
	c->copy = 0;
	c->texture = 0;
}

static void drop_all(int deleteCopies)
{
	for (int i = 0; i < g_copyCount; i++) drop(&g_copies[i], deleteCopies);
	g_copyCount = 0;
}

/* Our resampled copy of texture for scale s, or 0 */
static u32 make_copy(u8 *texture, float s)
{
	int gw = 0, gh = 0, dw, dh, clampU, clampV;
	u8 *src, *dst;
	u32 copy = 0;

	dw = (int)(TXMP_WIDTH(texture) * s + 0.5f);
	dh = (int)(TXMP_HEIGHT(texture) * s + 0.5f);
	if (dw < 1 || dh < 1 || dw > cMaxSide || dh > cMaxSide) return 0;
	for (int i = 0; i < 32 && gl.GetError() != GL_NO_ERROR; i++) {}
	bind_unit0(TXMP_GLNAME(texture));
	gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gw);
	gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &gh);
	if (gw < 1 || gh < 1 || gw > cMaxSide || gh > cMaxSide) return 0;

	src = VirtualAlloc(0, (u32)gw * gh * 4 + (u32)dw * dh * 4, 0x3000, 4);
	if (!src) return 0;
	dst = src + (u32)gw * gh * 4;
	pixel_store_default();
	gl.GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
	clampU = (TXMP_FLAGS(texture) & 0x4) != 0;
	clampV = (TXMP_FLAGS(texture) & 0x8) != 0;
	if (gl.GetError() == GL_NO_ERROR && resample(src, gw, gh, dst, dw, dh, clampU, clampV)) {
		gl.GenTextures(1, &copy);
		bind_unit0(copy);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
		if (gl.GetError() != GL_NO_ERROR) {
			gl.DeleteTextures(1, &copy);
			copy = 0;
		}
	}
	pixel_store_restore();
	VirtualFree(src, 0, 0x8000);
	if (!copy) log_msg("hud smooth: no copy of a %dx%d texture at %d%%", gw, gh, (int)(s * 100.0f + 0.5f));
	return copy;
}

static u32 copy_for(u8 *texture, float s)
{
	Copy *c = 0;

	for (int i = 0; i < g_copyCount; i++)
		if (g_copies[i].texture == texture) { c = &g_copies[i]; break; }
	if (c && c->name == TXMP_GLNAME(texture) && c->flags == TXMP_FLAGS(texture) &&
	    c->width == TXMP_WIDTH(texture) && c->height == TXMP_HEIGHT(texture) && c->scale == s)
		return c->copy;
	if (c) drop(c, 1);
	else if (g_copyCount < (int)(sizeof g_copies / sizeof g_copies[0])) c = &g_copies[g_copyCount++];
	else { c = &g_copies[g_nextEvict]; g_nextEvict = (g_nextEvict + 1) % g_copyCount; drop(c, 1); }
	c->texture = texture;
	c->name = TXMP_GLNAME(texture);
	c->flags = TXMP_FLAGS(texture);
	c->width = TXMP_WIDTH(texture);
	c->height = TXMP_HEIGHT(texture);
	c->scale = s;
	c->copy = make_copy(texture, s);   /* 0 is kept too, so a failure isn't retried every frame */
	return c->copy;
}

/* ---- For hud_scale.c ------------------------------------------------------------------------ */

void hud_smooth_frame(void)
{
	void *context;
	int level;

	if (!load_gl()) return;
	context = gl.GetCurrentContext();
	level = ONrLevel_GetCurrent();
	if (context != g_context) drop_all(0);   /* its names went with the old context */
	else if (level != g_level) drop_all(1);
	g_context = context;
	g_level = level;
}

u8 *hud_smooth_swap(float s, u32 *saved)
{
	u8 *texture = M3gDrawState ? M3gDrawState[4] : 0;
	u32 copy;

	if (!texture || !gl.ok || !g_context || !TXMP_GLNAME(texture) || TXMP_DIRTY(texture)) return 0;
	if (!(copy = copy_for(texture, s))) return 0;
	*saved = TXMP_GLNAME(texture);
	TXMP_GLNAME(texture) = copy;
	GLgBound = cNotBound;
	return texture;
}

void hud_smooth_restore(u8 *texture, u32 saved)
{
	if (!texture) return;
	TXMP_GLNAME(texture) = saved;
	GLgBound = cNotBound;
}
