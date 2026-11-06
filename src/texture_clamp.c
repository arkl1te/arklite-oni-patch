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

void texture_clamp_install(void)
{
	int s = patch_push_imm32(GLcTexture_WrapSPush, GL_CLAMP, GL_CLAMP_TO_EDGE);
	int t = patch_push_imm32(GLcTexture_WrapTPush, GL_CLAMP, GL_CLAMP_TO_EDGE);

	if (s && t)
		log_msg("texture clamp: clamp to edge (no skybox seams)");
	texture_buffer_install();
	sky_bottom_install();
}
