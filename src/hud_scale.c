/* HUD scale (Options > More... > HUD Scale).
 *
 * The HUD (ONiInGameUI_DisplayHUD, 0x4fff50, called once a frame at 0x4fbbc4) is laid out in
 * screen pixels at a fixed size (0x504270): the health ring's center sits half its background
 * texture in from the bottom-left corner, the weapon ring's from the bottom-right, and the
 * rings have fixed radii (40..64 px). So it gets smaller as the resolution goes up.
 *
 * Everything the HUD draws goes through two Motoko primitives, called with the screen points
 * in ECX and the texture coordinates in EDX (points are {x, y, z, 1/w}, 16 bytes):
 *   - 0x41a5c0 textured triangle, 3 points: every ring arc (built by 0x5007d0, drawn by
 *     0x500640).
 *   - 0x41a5e0 sprite, 2 points (top-left, bottom-right): the ring backgrounds and the other
 *     HUD pictures.
 * We route the 7 HUD call sites through functions that scale the points toward the screen
 * corner nearest to the primitive (each HUD element lives in one corner), only while the HUD
 * is drawing. The F1 help overlay (0x501440, text placed from the unscaled layout), the
 * countdown timer text (0x4fd500) and the overlay drawn by 0x501330 are not scaled. */
#include "arklite.h"

#define M3rDraw_Triangle ((u32 (ONI_FC *)(const float *, const void *))0x41a5c0)
#define M3rDraw_Sprite   ((u32 (ONI_FC *)(const float *, const void *))0x41a5e0)
#define M3rScreen_Width  ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height ((u16 (ONI_FC *)(void))0x41e130)
#define ONiInGameUI_DisplayHUD ((void (*)(void))0x4fff50)
#define ONcHUD_DisplayCall 0x4fbbc4

static const u32 g_triangleCalls[] = { 0x50073a, 0x5007af };
static const u32 g_spriteCalls[] = { 0x5000f2, 0x50045d, 0x50053a, 0x500fdb, 0x501309 };

static int g_inHud;

/* Copies n points into out, scaled by s toward the screen corner nearest their center. */
static void scale_points(const float *in, float *out, int n, float s)
{
	float w = (float)M3rScreen_Width(), h = (float)M3rScreen_Height();
	float cx = 0.0f, cy = 0.0f, ax, ay;

	for (int i = 0; i < n * 4; i++) out[i] = in[i];
	for (int i = 0; i < n; i++) { cx += in[i * 4]; cy += in[i * 4 + 1]; }
	ax = cx / n < w * 0.5f ? 0.0f : w;
	ay = cy / n < h * 0.5f ? 0.0f : h;
	for (int i = 0; i < n; i++) {
		out[i * 4]     = ax + (in[i * 4] - ax) * s;
		out[i * 4 + 1] = ay + (in[i * 4 + 1] - ay) * s;
	}
}

static u32 ONI_FC hud_triangle(const float *points, const void *uvs)
{
	float scaled[3 * 4];
	float s = hud_scale();

	if (!g_inHud || s == 1.0f) return M3rDraw_Triangle(points, uvs);
	scale_points(points, scaled, 3, s);
	return M3rDraw_Triangle(scaled, uvs);
}

static u32 ONI_FC hud_sprite(const float *points, const void *uvs)
{
	float scaled[2 * 4];
	float s = hud_scale();

	if (!g_inHud || s == 1.0f) return M3rDraw_Sprite(points, uvs);
	scale_points(points, scaled, 2, s);
	return M3rDraw_Sprite(scaled, uvs);
}

static void hud_display(void)
{
	g_inHud = 1;
	ONiInGameUI_DisplayHUD();
	g_inHud = 0;
}

void hud_scale_install(void)
{
	int n = 0, total = 1;

	for (unsigned i = 0; i < sizeof g_triangleCalls / sizeof g_triangleCalls[0]; i++, total++)
		n += patch_call(g_triangleCalls[i], (u32)M3rDraw_Triangle, (u32)hud_triangle);
	for (unsigned i = 0; i < sizeof g_spriteCalls / sizeof g_spriteCalls[0]; i++, total++)
		n += patch_call(g_spriteCalls[i], (u32)M3rDraw_Sprite, (u32)hud_sprite);
	n += patch_call(ONcHUD_DisplayCall, (u32)ONiInGameUI_DisplayHUD, (u32)hud_display);
	log_msg("hud scale: %d of %d patches applied", n, total);
}
