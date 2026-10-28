/* HUD scale (Options > More... > HUD Scale).
 *
 * The HUD (ONiInGameUI_DisplayHUD, 0x4fff50, called once a frame at 0x4fbbc4) is laid out in
 * screen pixels at a fixed size (0x504270): the left ring's center (0x5ecfa0) sits half its
 * background texture in from the bottom-left corner, the right ring's (0x5ed010) from the
 * bottom-right, and the rings have fixed radii (40..64 px). So it gets smaller as the resolution
 * goes up.
 *
 * Everything the HUD draws goes through two Motoko primitives, called with the screen points
 * in ECX and the texture coordinates in EDX (points are {x, y, z, 1/w}, 16 bytes):
 *   - 0x41a5c0 textured triangle, 3 points: every ring arc (built by 0x5007d0, drawn by
 *     0x500640).
 *   - 0x41a5e0 sprite, 2 points (top-left, bottom-right): the ring backgrounds, the other HUD
 *     pictures, the 54 px icon 0x501330 puts in the right ring, and every glyph of text (the
 *     text context's line drawer 0x42dac0 calls it at 0x42deae).
 * We route the HUD's call sites through functions that scale the points toward the screen
 * corner nearest to the primitive (each HUD element lives in one corner), only while the HUD
 * is drawing.
 *
 * Pictures: hud_smooth.c swaps each scaled picture's texture for a copy resampled to its size
 * on screen (Lanczos), and sprites go on whole pixels so it lands one texel per pixel. Plain
 * GL_LINEAR stretching looked blurred, GL_NEAREST blocky.
 *
 * Text is not stretched: Oni's Tahoma comes in 7, 9, 10, 12 and 14 pt (TSFFTahoma, level0), and
 * the text is drawn with the size nearest to the scaled height, at its own pixels. Only the
 * point it hangs from is moved, toward its ring's corner. When no size is near (the larger
 * scales), a size is doubled or tripled with nearest filtering instead, and below 100% the 7 pt
 * is shrunk. Text on the HUD:
 *   - the F1 help labels: each is laid out by 0x42e210 (ECX context *0x5ecfb4, EDX the item,
 *     stack: bounds, the point {x, y} as shorts, the result; at 0x501790) and drawn twice
 *     (shadow, then text) by 0x42d9e0. Left ring labels hang from their left edge, right ring
 *     labels from their right; both are centered on the point vertically. Items below help
 *     +0x2c are the left ring's, the rest the right's (items are 0x44 bytes from help +0x38;
 *     help = *0x5ecf9c). The help pictures under them (the lines) are sprites at 0x501608
 *     (left) and 0x5016cf (right).
 *   - the countdown timer (0x4fd500, ECX = the point {x, y} as floats it centers on, the right
 *     ring's center; called at 0x50061f), on the game's text context *0x5ece70.
 * A text context (0x42ea30) holds the family (+0x0), the font (+0x4), the size (+0x10) and the
 * style (+0x14); 0x42ed50 (ECX context, DX size) sets the size and picks that font. A family
 * lists its fonts at +0x18 (+0x14 of them); a font has its size at +0x6, style +0x8, ascender
 * +0xc, descender +0xe.
 * The glyph call and the timer call belong to mp_hud.c (its own text is drawn there at twice
 * Oni's size), which passes the rest to hud_text_sprite and hud_timer_display.
 *
 * Subtitles: the subtitle area (*0x5cb470) and the message area (*0x5cb398), which shows when
 * there are no subtitles, are text areas laid out by 0x42fb70 between the rings: bounds at +0x12
 * (left, top, right, bottom as shorts) = 148, bottom - 10 lines, width - 148, height - 2, 148 px
 * being a ring's box (128 px background, 10 px in from the corner, 10 px gap). 0x430400 draws
 * one of them (called at 0x4fbde3, after the HUD), wrapping the text to the bounds every frame
 * (0x430fd0). On a frame the HUD drew at a scale above 100%, both get the gap between the scaled
 * rings for that draw; when that is narrower than cSubtitleMinWidth of the screen, they go above
 * the rings instead (ring height: the backgrounds *0x5ed020 left, *0x5ecf98 right, +0x86). */
#include "arklite.h"

#define M3rDraw_Triangle ((u32 (ONI_FC *)(const float *, const void *))0x41a5c0)
#define M3rDraw_Sprite   ((u32 (ONI_FC *)(const float *, const void *))0x41a5e0)
#define M3rScreen_Width  ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height ((u16 (ONI_FC *)(void))0x41e130)
#define M3gDrawState     (*(u8 ***)0x6b8cb0)   /* [4] the texture, as committed for the draw */
#define TSrContext_Layout ((u16 (ONI_FC *)(void *, const u8 *, void *, short *, void *))0x42e210)
#define TSrContext_SetFontSize ((u16 (ONI_FC *)(void *, u32))0x42ed50)
#define ONrTimer_Display ((void (ONI_FC *)(void *))0x4fd500)
#define ONiInGameUI_DisplayHUD ((void (*)(void))0x4fff50)
#define ONcHUD_DisplayCall 0x4fbbc4
#define ONgTimerContext  (*(u8 **)0x5ece70)
#define ONgLeftRing  ((const float *)0x5ecfa0)   /* center {x, y} */
#define ONgRightRing ((const float *)0x5ed010)
#define ONgHelp      (*(u8 **)0x5ecf9c)
#define ONcHelp_LeftSpriteCall  0x501608
#define ONcHelp_RightSpriteCall 0x5016cf
#define ONcHelp_LayoutCall      0x501790
#define ONrSubtitles_Display ((void (*)(void))0x430400)
#define ONcSubtitles_DisplayCall 0x4fbde3
#define ONgSubtitleArea (*(u8 **)0x5cb470)
#define ONgMessageArea  (*(u8 **)0x5cb398)
#define ONgLeftRingBackground  (*(u8 **)0x5ed020)
#define ONgRightRingBackground (*(u8 **)0x5ecf98)

/* The OpenGL driver: its table of GL functions (stdcall), its state and the texture it last
 * bound on unit 0, which it doesn't bind again (0x40d74f) */
#define GLgFunctions (*(u8 **)0x560604)
#define GLgDriver    (*(u8 **)0x560600)
#define GLgBound     (*(u8 **)0x560614)
#define GL_TEXTURE_2D         0x0de1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_TEXTURE0           0x84c0

/* How near a font's height must be to the scaled height to be used */
#define cFontTolerance 1.15f

/* The narrowest the subtitles may get between the rings, of the screen's width */
#define cSubtitleMinWidth 0.4f

static const u32 g_triangleCalls[] = { 0x50073a, 0x5007af };
static const u32 g_spriteCalls[] = { 0x5000f2, 0x50045d, 0x50053a, 0x500fdb, 0x501309, 0x50141b };

static int g_inHud;
static int g_hudDrawn;   /* this frame, for the subtitles */

/* Textures switched to GL_NEAREST this frame */
static u8 *g_sharp[64];
static int g_sharpCount;

/* The text being drawn: its glyphs are scaled by g_textScale about g_textX, g_textY */
static int g_textOn, g_textSharp;
static float g_textScale, g_textX, g_textY;

/* The help labels' font this frame: their context, its own size, and the scale left */
static u8 *g_helpContext;
static u32 g_helpSize;
static int g_helpSharp;
static float g_helpScale;

static float round_px(float v) { return (float)(int)(v < 0.0f ? v - 0.5f : v + 0.5f); }

/* The screen corner nearest to x, y */
static void nearest_corner(float x, float y, float *ax, float *ay)
{
	float w = (float)M3rScreen_Width(), h = (float)M3rScreen_Height();

	*ax = x < w * 0.5f ? 0.0f : w;
	*ay = y < h * 0.5f ? 0.0f : h;
}

/* Copies n points into out, scaled by s toward ax, ay */
static void scale_about(const float *in, float *out, int n, float s, float ax, float ay)
{
	for (int i = 0; i < n * 4; i++) out[i] = in[i];
	for (int i = 0; i < n; i++) {
		out[i * 4]     = ax + (in[i * 4] - ax) * s;
		out[i * 4 + 1] = ay + (in[i * 4 + 1] - ay) * s;
	}
}

/* Copies n points into out, scaled by s toward the screen corner nearest their center. */
static void scale_points(const float *in, float *out, int n, float s)
{
	float cx = 0.0f, cy = 0.0f, ax, ay;

	for (int i = 0; i < n; i++) { cx += in[i * 4]; cy += in[i * 4 + 1]; }
	nearest_corner(cx / n, cy / n, &ax, &ay);
	scale_about(in, out, n, s, ax, ay);
}

/* Sets a texture's magnification filter the way the driver binds a texture on unit 0
 * (0x40d767), so its record of what is bound stays true */
static void set_mag_filter(u8 *texture, u32 filter)
{
	u8 *gl = GLgFunctions, *driver = GLgDriver;
	u32 name;

	if (!gl || !driver || !texture || !(name = *(u32 *)(texture + 0x9c))) return;
	if (*(u32 *)(driver + 0x584)) {
		(*(void (__stdcall **)(u32))(gl + 0x568))(GL_TEXTURE0);   /* glActiveTextureARB */
		(*(void (__stdcall **)(u32))(gl + 0x56c))(GL_TEXTURE0);   /* glClientActiveTextureARB */
	}
	(*(void (__stdcall **)(u32, u32))(gl + 0x14))(GL_TEXTURE_2D, name);   /* glBindTexture */
	GLgBound = texture;
	(*(void (__stdcall **)(u32, u32, u32))(gl + 0x4c0))(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

/* The texture the primitive about to draw uses gets GL_NEAREST until the HUD is done */
static void sharpen_texture(void)
{
	u8 *texture = M3gDrawState ? M3gDrawState[4] : 0;

	if (!texture || !*(u32 *)(texture + 0x9c)) return;
	for (int i = 0; i < g_sharpCount; i++)
		if (g_sharp[i] == texture) return;
	if (g_sharpCount == sizeof g_sharp / sizeof g_sharp[0]) return;
	g_sharp[g_sharpCount++] = texture;
	set_mag_filter(texture, GL_NEAREST);
}

static void unsharpen_textures(void)
{
	for (int i = 0; i < g_sharpCount; i++) set_mag_filter(g_sharp[i], GL_LINEAR);
	g_sharpCount = 0;
}

/* Draws a scaled HUD picture with its texture swapped for a copy resampled to the scale
 * (hud_smooth.c); a sprite then goes on whole pixels, one texel of the copy per pixel. */
static u32 draw_picture(u32 (ONI_FC *draw)(const float *, const void *), float *points, int n,
                        const void *uvs, float s)
{
	u32 saved = 0, result;
	u8 *texture = hud_smooth_swap(s, &saved);

	if (texture && n == 2) {
		for (int k = 0; k < 2; k++) {
			float size = round_px(points[4 + k] - points[k]);
			points[k] = round_px(points[k]);
			points[4 + k] = points[k] + size;
		}
	}
	result = draw(points, uvs);
	hud_smooth_restore(texture, saved);
	return result;
}

static u32 ONI_FC hud_triangle(const float *points, const void *uvs)
{
	float scaled[3 * 4];
	float s = hud_scale();

	if (!g_inHud || s == 1.0f) return M3rDraw_Triangle(points, uvs);
	scale_points(points, scaled, 3, s);
	return draw_picture(M3rDraw_Triangle, scaled, 3, uvs, s);
}

static u32 ONI_FC hud_sprite(const float *points, const void *uvs)
{
	float scaled[2 * 4];
	float s = hud_scale();

	if (!g_inHud || s == 1.0f) return M3rDraw_Sprite(points, uvs);
	scale_points(points, scaled, 2, s);
	return draw_picture(M3rDraw_Sprite, scaled, 2, uvs, s);
}

/* A sprite scaled toward the corner of the ring centered at ring */
static u32 ring_sprite(const float *ring, const float *points, const void *uvs)
{
	float scaled[2 * 4], ax, ay;
	float s = hud_scale();

	if (!g_inHud || s == 1.0f) return M3rDraw_Sprite(points, uvs);
	nearest_corner(ring[0], ring[1], &ax, &ay);
	scale_about(points, scaled, 2, s, ax, ay);
	return draw_picture(M3rDraw_Sprite, scaled, 2, uvs, s);
}

static u32 ONI_FC help_left_sprite(const float *points, const void *uvs)
{
	return ring_sprite(ONgLeftRing, points, uvs);
}

static u32 ONI_FC help_right_sprite(const float *points, const void *uvs)
{
	return ring_sprite(ONgRightRing, points, uvs);
}

static float glyph_height(const u8 *font)
{
	return (float)(*(const short *)(font + 0xc) + *(const short *)(font + 0xe));
}

/* Gives the context the font for scale s: the size whose glyphs are nearest to s times the
 * current ones, drawn as they are; else one doubled or tripled with nearest filtering (*sharp);
 * else (below 100%) the nearest size, shrunk. Returns the scale left for the glyphs. */
static float pick_font(u8 *context, float s, int *sharp)
{
	u8 *family = *(u8 **)context, *current = *(u8 **)(context + 0x4), *best = 0, *nearest = 0;
	u32 style = *(u32 *)(context + 0x14), count;
	float target, bestError = 0.0f, nearestError = 0.0f;
	int bestTimes = 1;

	*sharp = 0;
	if (!family || !current || glyph_height(current) <= 0.0f) return s;
	target = glyph_height(current) * s;
	count = *(u32 *)(family + 0x14);
	for (int times = 1; times <= 3; times++) {
		for (u32 i = 0; i < count; i++) {
			u8 *font = ((u8 **)(family + 0x18))[i];
			float q, error;

			if (!font || *(u32 *)(font + 0x8) != style || glyph_height(font) <= 0.0f) continue;
			q = glyph_height(font) * times / target;
			error = q > 1.0f ? q : 1.0f / q;
			if (times == 1 && (!nearest || error < nearestError)) { nearest = font; nearestError = error; }
			if (!best || error < bestError) { best = font; bestError = error; bestTimes = times; }
		}
		if (best && bestError <= cFontTolerance) break;   /* fewer times wins */
	}
	if (!best) return s;
	if (bestError > cFontTolerance) { best = nearest; bestTimes = 0; }
	if (TSrContext_SetFontSize(context, *(u16 *)(best + 0x6)) != 0 || *(u8 **)(context + 0x4) != best) {
		TSrContext_SetFontSize(context, *(u16 *)(current + 0x6));   /* as it was */
		return s;
	}
	if (!bestTimes) return target / glyph_height(best);
	*sharp = bestTimes > 1;
	return (float)bestTimes;
}

/* Every glyph of every string the text context draws */
u32 ONI_FC hud_text_sprite(const float *points, const void *uvs)
{
	float scaled[2 * 4];

	if (!g_inHud || !g_textOn || g_textScale == 1.0f) return M3rDraw_Sprite(points, uvs);
	scale_about(points, scaled, 2, g_textScale, g_textX, g_textY);
	if (g_textSharp) sharpen_texture();
	return M3rDraw_Sprite(scaled, uvs);
}

void ONI_FC hud_timer_display(void *center)
{
	const float *c = (const float *)center;
	u8 *context = ONgTimerContext;
	float s = hud_scale(), at[2], ax, ay;
	u32 size;

	if (!g_inHud || s == 1.0f || !context) { ONrTimer_Display(center); return; }
	nearest_corner(c[0], c[1], &ax, &ay);
	at[0] = round_px(ax + (c[0] - ax) * s);
	at[1] = round_px(ay + (c[1] - ay) * s);
	size = *(u16 *)(context + 0x10);
	g_textScale = pick_font(context, s, &g_textSharp);
	g_textX = at[0];
	g_textY = at[1];
	g_textOn = 1;
	ONrTimer_Display(at);
	g_textOn = 0;
	TSrContext_SetFontSize(context, size);   /* mp_hud.c draws with it next */
}

/* Lays out a help label: from its point moved toward its ring's corner, in the font picked
 * for the frame. Its glyphs follow, until the next label. */
static u16 ONI_FC help_layout(u8 *context, const u8 *item, void *bounds, short *point, void *out)
{
	const u8 *help = ONgHelp;
	u32 index = (u32)(item - (help + 0x38)) / 0x44;
	const float *ring = index < *(const u32 *)(help + 0x2c) ? ONgLeftRing : ONgRightRing;
	float s = hud_scale(), ax, ay;

	if (!g_inHud || s == 1.0f || !context) return TSrContext_Layout(context, item, bounds, point, out);
	if (g_helpContext != context) {
		if (g_helpContext) TSrContext_SetFontSize(g_helpContext, g_helpSize);
		g_helpContext = context;
		g_helpSize = *(u16 *)(context + 0x10);
		g_helpScale = pick_font(context, s, &g_helpSharp);
	}
	nearest_corner(ring[0], ring[1], &ax, &ay);
	point[0] = (short)round_px(ax + (point[0] - ax) * s);
	point[1] = (short)round_px(ay + (point[1] - ay) * s);
	g_textScale = g_helpScale;
	g_textSharp = g_helpSharp;
	g_textX = point[0];
	g_textY = point[1];
	g_textOn = 1;
	return TSrContext_Layout(context, item, bounds, point, out);
}

/* The taller ring background's height */
static int ring_height(void)
{
	const u8 *left = ONgLeftRingBackground, *right = ONgRightRingBackground;
	int h = left ? *(const u16 *)(left + 0x86) : 128;

	if (right && *(const u16 *)(right + 0x86) > h) h = *(const u16 *)(right + 0x86);
	return h;
}

/* Gives a text area the room the rings scaled by s leave it; its bounds go to saved */
static void subtitle_bounds(u8 *area, short *saved, float s)
{
	short *r = (short *)(area + 0x12);
	int w = M3rScreen_Width(), h = M3rScreen_Height(), left, right, bottom, dy;

	for (int i = 0; i < 4; i++) saved[i] = r[i];
	left = (int)round_px(r[0] * s);
	right = w - (int)round_px((w - r[2]) * s);
	if (right - left >= w * cSubtitleMinWidth) {
		r[0] = (short)left;
		r[2] = (short)right;
		return;
	}
	/* No room between them: above them, as wide as at 100% */
	bottom = h - (int)round_px(ring_height() * s) - 2;
	dy = r[3] - bottom;
	if (dy <= 0 || bottom < 32) return;
	r[3] = (short)bottom;
	r[1] = (short)(r[1] - dy < 2 ? 2 : r[1] - dy);
}

static void subtitles_display(void)
{
	u8 *areas[2] = { ONgSubtitleArea, ONgMessageArea };
	short saved[2][4];
	float s = hud_scale();
	int scaled = g_hudDrawn && s > 1.0f;

	g_hudDrawn = 0;
	if (!scaled) { ONrSubtitles_Display(); return; }
	for (int i = 0; i < 2; i++)
		if (areas[i]) subtitle_bounds(areas[i], saved[i], s);
	ONrSubtitles_Display();
	for (int i = 0; i < 2; i++)
		if (areas[i])
			for (int k = 0; k < 4; k++) ((short *)(areas[i] + 0x12))[k] = saved[i][k];
}

static void hud_display(void)
{
	if (hud_scale() != 1.0f) hud_smooth_frame();
	g_hudDrawn = 1;
	g_inHud = 1;
	ONiInGameUI_DisplayHUD();
	g_inHud = 0;
	g_textOn = 0;
	if (g_helpContext) {
		TSrContext_SetFontSize(g_helpContext, g_helpSize);
		g_helpContext = 0;
	}
	unsharpen_textures();
}

void hud_scale_install(void)
{
	int n = 0, total = 5;

	for (unsigned i = 0; i < sizeof g_triangleCalls / sizeof g_triangleCalls[0]; i++, total++)
		n += patch_call(g_triangleCalls[i], (u32)M3rDraw_Triangle, (u32)hud_triangle);
	for (unsigned i = 0; i < sizeof g_spriteCalls / sizeof g_spriteCalls[0]; i++, total++)
		n += patch_call(g_spriteCalls[i], (u32)M3rDraw_Sprite, (u32)hud_sprite);
	n += patch_call(ONcHelp_LeftSpriteCall, (u32)M3rDraw_Sprite, (u32)help_left_sprite);
	n += patch_call(ONcHelp_RightSpriteCall, (u32)M3rDraw_Sprite, (u32)help_right_sprite);
	n += patch_call(ONcHelp_LayoutCall, (u32)TSrContext_Layout, (u32)help_layout);
	n += patch_call(ONcHUD_DisplayCall, (u32)ONiInGameUI_DisplayHUD, (u32)hud_display);
	n += patch_call(ONcSubtitles_DisplayCall, (u32)ONrSubtitles_Display, (u32)subtitles_display);
	log_msg("hud scale: %d of %d patches applied", n, total);
}
