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
 * fringe. Oni's other texture path (0x488750) already uses GL_CLAMP_TO_EDGE. */
#include "arklite.h"

#define GL_CLAMP              0x2900
#define GL_CLAMP_TO_EDGE      0x812f
#define GLcTexture_WrapSPush  0x40d018   /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ..) */
#define GLcTexture_WrapTPush  0x40d058   /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ..) */

void texture_clamp_install(void)
{
	int s = patch_push_imm32(GLcTexture_WrapSPush, GL_CLAMP, GL_CLAMP_TO_EDGE);
	int t = patch_push_imm32(GLcTexture_WrapTPush, GL_CLAMP, GL_CLAMP_TO_EDGE);

	if (s && t)
		log_msg("texture clamp: clamp to edge (no skybox seams)");
}
