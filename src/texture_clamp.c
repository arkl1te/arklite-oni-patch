/* No black seams on the skybox.
 *
 * The OpenGL texture upload (0x40cf40) sets a texture's wrap mode from its TXMP flags: 0x4
 * (no U wrap) and 0x8 (no V wrap) give GL_CLAMP, otherwise GL_REPEAT. GL_CLAMP with GL_LINEAR
 * filtering blends the edge texels with the border color, black by default, so a clamped
 * texture drawn with UVs 0..1 gets a dark half-texel fringe on every edge. Every skybox face
 * (ONSK, e.g. TXMPsunset_front) is flagged 0x101c and drawn on a cube of quads with UVs 0..1
 * (0x412fc0), so each cube edge showed as a black line. Drivers of 2001 often treated GL_CLAMP
 * as clamp-to-edge, which hid it. We push GL_CLAMP_TO_EDGE instead: the edge texel is
 * repeated and no border is sampled. That covers every clamped texture, which only loses the
 * fringe. Oni's other texture path (0x488750) already uses GL_CLAMP_TO_EDGE.
 *
 * Bigger textures, too. The upload (0x40cf40) converts a texture's pixels into a buffer of
 * 256x256x4 bytes, allocated once at 0x40524e, before handing them to OpenGL. Every format
 * OniSplit writes goes through it (0x530860 is the format table), so a texture past 65536
 * pixels ran off its end. Catwalk's levels may use 1024x1024, so the buffer takes that.
 *
 * A floor for the sky, too. An ONSK links six textures and the skybox (M3tSkyboxData) has room for
 * six textures and six quads, but only five of each are used, so below the box nothing was drawn.
 * The create method (0x412e60) now copies the sixth texture, the draw (0x412fc0) goes on to it and
 * gets its quad here. The draw skips a NULL texture, and no sky of the game has a sixth: only
 * Catwalk's do, so the game's skies draw as before. */
#include "arklite.h"

#define GL_CLAMP              0x2900
#define GL_CLAMP_TO_EDGE      0x812f
#define GLcTexture_WrapSPush  0x40d018   /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ..) */
#define GLcTexture_WrapTPush  0x40d058   /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ..) */

#define GLcConvertBufferMov   0x40524e   /* mov ecx, size; call UUrMemory_Block_New */
#define GLcConvertBufferOld   0x40000    /* 256 * 256 * 4 */
#define GLcConvertBufferNew   0x400000   /* 1024 * 1024 * 4 */
#define GLcBlockNew           0x424a50   /* UUrMemory_Block_New, called right after the mov */

static void texture_buffer_install(void)
{
	/* Daodan's modding.largetextures gets here first and makes it 512x512, so the size can be any
	 * that isn't ours yet. */
	const u8 *code = (const u8 *)GLcConvertBufferMov;
	u32 size = GLcConvertBufferNew;
	u32 now = *(const u32 *)(code + 1);

	if (code[0] != 0xb9 || now < GLcConvertBufferOld || now > GLcConvertBufferNew ||
	    code[5] != 0xe8 || GLcConvertBufferMov + 10 + *(const u32 *)(code + 6) != GLcBlockNew) {
		log_msg("texture buffer: unexpected code at 0x40524e, left alone");
		return;
	}
	patch_write(GLcConvertBufferMov + 1, &size, 4);
	log_msg("texture buffer: up to 1024x1024 (was %d bytes)", now);
}

#define SkyCreateCount  0x412e73   /* mov edi, 5: the textures it copies */
#define SkyCreateEnd    0x412fbb   /* xor ax, ax; pop esi; ret */
#define SkyDrawCount    0x413099   /* mov dword [esp + 0x14], 5: the quads it draws */

/* The create method's end: ecx is the skybox, whose corners 4 to 7 are its underside, with the UVs
 * 0,0 1,0 1,1 0,1 of every quad: u = (x + 1) / 2, v = (z + 1) / 2. */
__attribute__((naked)) static void sky_create_end(void)
{
	__asm__("movl $4, 0x6c(%ecx)\n\t"
	        "movl $5, 0x70(%ecx)\n\t"
	        "movl $6, 0x74(%ecx)\n\t"
	        "movl $7, 0x78(%ecx)\n\t"
	        "xorw %ax, %ax\n\t"
	        "popl %esi\n\t"
	        "ret");
}

static void sky_bottom_install(void)
{
	static const u8 count[] = {0xbf, 5, 0, 0, 0};
	static const u8 end[] = {0x66, 0x33, 0xc0, 0x5e, 0xc3};
	static const u8 draw[] = {0xc7, 0x44, 0x24, 0x14, 5, 0, 0, 0};
	u8 jump[5] = {0xe9};
	u32 rel = (u32)sky_create_end - (SkyCreateEnd + 5);
	u8 six = 6;
	int i;

	if (!code_matches(SkyCreateCount, count, sizeof count) ||
	    !code_matches(SkyCreateEnd, end, sizeof end) ||
	    !code_matches(SkyDrawCount, draw, sizeof draw)) {
		log_msg("sky bottom: unexpected code, left alone");
		return;
	}
	for (i = 0; i < 4; i++)
		jump[i + 1] = (u8)(rel >> (i * 8));
	patch_write(SkyCreateCount + 1, &six, 1);
	patch_write(SkyCreateEnd, jump, sizeof jump);
	patch_write(SkyDrawCount + 4, &six, 1);
	log_msg("sky bottom: sixth face on");
}

/* A sky that reaches the corners of a wide screen.
 *
 * The box is ±5 around the camera, and the near plane is 2, but the clip that cuts the near side
 * (0x418100, clip code 0x20) tests clip z < 0 while the projection (0x41aeb0) is OpenGL's, whose
 * z runs from -w to w: z = 0 lies at the depth 2nf / (n + f), about 4. Whatever of the box is
 * closer than that is cut away and shows black. Near that depth the view reaches as far as
 * 4 * sqrt(1 + tan²(h/2) + tan²(v/2)) from the camera: 4.86 at 4:3, inside the box, but 5.24 at
 * 16:9 and 5.87 at 21:9, so the bottom and side corners of the box went black when facing them.
 *
 * The draw now sizes the box to clear that depth with the camera it is about to use. A box
 * around the camera looks the same at any size (same directions, same perspective-correct UVs),
 * so only the cut changes. It never gets smaller than the game's 5, so at 4:3 nothing changes,
 * and it stays inside the far plane. Fog goes by the same projected z; at 21:9 the farthest
 * corner reaches z 0.62, below the lowest fog start any level sets (0.8). */
#define SkyDrawEntry      0x412fc0   /* sub esp, 0x1d0; push esi; mov esi, ecx: ecx is the skybox */
#define M3gGeomContext    (*(u8 **)0x560a28)   /* +0x58: the camera the draw transforms with */
#define SkyCubePoints     0x7c       /* M3tSkyboxData: 8 corners, x y z each */
#define SkySize           5.0f       /* the create method's sky_distance */

static float sky_sqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static float sky_tan(float x) { float r; __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x)); return r; }

static void __cdecl sky_fit(u8 *sky)
{
	u8 *cam = M3gGeomContext ? *(u8 **)(M3gGeomContext + 0x58) : 0;
	float size = SkySize;

	if (!sky) return;
	if (cam) {
		float fovy = *(float *)(cam + 0x38), aspect = *(float *)(cam + 0x3c);
		float n = *(float *)(cam + 0x40), f = *(float *)(cam + 0x44);
		if (fovy > 0.0f && fovy < 3.1f && aspect > 0.0f && n > 0.0f && f > n) {
			float cut = 2.0f * n * f / (n + f);   /* depth of clip z = 0 */
			float tv = sky_tan(fovy * 0.5f), th = tv * aspect;
			float need = cut * sky_sqrt(1.0f + th * th + tv * tv) * 1.02f;
			float most = f * 0.9f / 1.7320508f;   /* the far corners stay inside the far plane */
			if (need > size) size = need;
			if (size > most && most >= SkySize) size = most;
		}
	}
	for (int i = 0; i < 8 * 3; i++) {
		float *c = (float *)(sky + SkyCubePoints) + i;
		*c = *c < 0.0f ? -size : size;
	}
}

__attribute__((naked)) static void sky_draw_entry(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"pushl %%ecx\n\t"
		"call %c0\n\t"
		"addl $4, %%esp\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"subl $0x1d0, %%esp\n\t"   /* the instruction the jump replaced */
		"pushl $0x412fc6\n\t"
		"ret\n" :: "i"(sky_fit));
}

static void sky_fit_install(void)
{
	static const u8 entry[] = {0x81, 0xec, 0xd0, 0x01, 0x00, 0x00, 0x56, 0x8b, 0xf1};
	u8 jump[6] = {0xe9, 0, 0, 0, 0, 0x90};

	if (!code_matches(SkyDrawEntry, entry, sizeof entry)) {
		log_msg("sky size: unexpected code at 0x412fc0, left alone");
		return;
	}
	*(u32 *)(jump + 1) = (u32)sky_draw_entry - (SkyDrawEntry + 5);
	patch_write(SkyDrawEntry, jump, sizeof jump);
	log_msg("sky size: fits wide screens");
}

void texture_clamp_install(void)
{
	int s = patch_push_imm32(GLcTexture_WrapSPush, GL_CLAMP, GL_CLAMP_TO_EDGE);
	int t = patch_push_imm32(GLcTexture_WrapTPush, GL_CLAMP, GL_CLAMP_TO_EDGE);

	if (s && t)
		log_msg("texture clamp: clamp to edge (no skybox seams)");
	texture_buffer_install();
	sky_bottom_install();
	sky_fit_install();
}
