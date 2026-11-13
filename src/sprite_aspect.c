/* Screen-facing sprites keep their shape on wide screens.
 *
 * Unrotated, unoriented sprites (M3rSimpleSprite_Draw and every particle whose class draws as a
 * plain Sprite without rotation: the weapons' aiming rings and tunnel, glows, flashes) go through
 * MSiSprite_Draw_Unoriented, which sizes them in screen pixels:
 *     xScale = drawWidth  * 0.78125     (500 / 640, at 0x41198f)
 *     yScale = drawHeight * 1.0416667   (500 / 480)
 * Those are equal only at 4:3. The world's projection (0x41aeb0) divides x by the camera's
 * aspect (+0x3c), so at 3440x1440 the world has square pixels but the sprites came out
 * (3440/1440) / (4/3) = 1.79 times too wide (user's screenshots, 2026-11-13: the red aiming rings
 * as wide ellipses). The sky's star field (sprite arrays, 0x4124b4) does the same.
 *
 * Both multiplies now use drawWidth * 1.0416667 / aspect, with the camera the draw itself uses
 * (*0x560a28 +0x58, whose +0x40/+0x44 the sprite's near/far test reads), so a sprite gets the
 * same shape the world's projection gives anything else: at aspect 4/3 that is 0.78125 again
 * (4:3 unchanged), and if a camera ever keeps 4/3 on a wide screen (a stretched world), sprites
 * stretch along with it as before. Drawing only. ARKLITE_SPRITE_ASPECT=0 leaves Oni's. */
#include "arklite.h"

#define MScSpriteXScale       0x41198f   /* MSiSprite_Draw_Unoriented: fmul [0x529568], st0 = width */
#define MScSpriteArrayXScale  0x4124b4   /* the sprite array draw (sky stars): the same */

/* Replaces "fmul dword [0x529568]" (st0 = drawWidth): st0 *= 1.0416667 / aspect, or Oni's own
 * 0.78125 when there is no camera or its aspect is out of 0.25..8. Keeps every register and
 * the flags; uses no FPU stack slot. */
__attribute__((naked)) static void sprite_xscale(void)
{
	__asm__ volatile(
		"pushfl\n\t"
		"pushl %%eax\n\t"
		"movl 0x560a28, %%eax\n\t"
		"testl %%eax, %%eax\n\t"
		"jz 1f\n\t"
		"movl 0x58(%%eax), %%eax\n\t"
		"testl %%eax, %%eax\n\t"
		"jz 1f\n\t"
		"movl 0x3c(%%eax), %%eax\n\t"     /* aspect: positive floats sort like their bits */
		"cmpl $0x3e800000, %%eax\n\t"     /* 0.25 */
		"jb 1f\n\t"
		"cmpl $0x41000000, %%eax\n\t"     /* 8 */
		"ja 1f\n\t"
		"pushl %%eax\n\t"
		"fmuls 0x529564\n\t"              /* the y scale's 500 / 480 */
		"fdivs (%%esp)\n\t"
		"addl $4, %%esp\n\t"
		"popl %%eax\n\t"
		"popfl\n\t"
		"ret\n"
		"1:\n\t"
		"fmuls 0x529568\n\t"              /* Oni's 500 / 640 */
		"popl %%eax\n\t"
		"popfl\n\t"
		"ret\n" ::);
}

static int sprite_hook(u32 addr)
{
	static const u8 fmul_xscale[] = {0xd8, 0x0d, 0x68, 0x95, 0x52, 0x00};
	u8 call[6] = {0xe8, 0, 0, 0, 0, 0x90};

	if (!code_matches(addr, fmul_xscale, sizeof fmul_xscale)) {
		log_msg("sprite aspect: unexpected code at 0x%x, left alone", addr);
		return 0;
	}
	*(u32 *)(call + 1) = (u32)sprite_xscale - (addr + 5);
	patch_write(addr, call, sizeof call);
	return 1;
}

void sprite_aspect_install(void)
{
	char v[8];
	if (GetEnvironmentVariableA("ARKLITE_SPRITE_ASPECT", v, sizeof v) && v[0] == '0') {
		log_msg("sprite aspect: left as Oni draws it (ARKLITE_SPRITE_ASPECT=0)");
		return;
	}
	int a = sprite_hook(MScSpriteXScale);
	int b = sprite_hook(MScSpriteArrayXScale);
	if (a && b)
		log_msg("sprite aspect: screen-facing sprites keep their shape on wide screens");
}
