/* Motion blur afterimages that stay see-through (the ninja's dashes, heavy punches and kicks).
 *
 * Oni draws them (ONrGameState_MotionBlur_Display, 0x4fcd00: every afterimage in the game state's
 * list, +0x158, 0x44 bytes each, count +0x1258, at its alpha) right after the characters (the call
 * at 0x4fb927, after 0x4e1540), with depth writes on like everything else. The particles (lightning,
 * sparks, glows) and the level's Transparent quads are drawn after that, and behind an afterimage
 * they failed the depth test: the afterimage was blended over whatever the frame held there (black
 * where only the sky or far glass was), so a dash seen against the sky or PowerPlant's far
 * structures left dark, almost solid boxes instead of fading copies (user's screenshot, 2026-11-12).
 *
 * Now the call draws them with depth writes off (draw state 4, ZWrite: M3rDraw_State_GetInt
 * 0x41e1a0 / SetInt 0x41e170, ECX the type, EDX the value; the next draw applies it), and puts the
 * old value back after: the depth test stays on, so walls in front still hide them, but they no
 * longer hide what's drawn behind them later. Drawing only. ARKLITE_MOTION_BLUR_FIX=0 leaves Oni's
 * (for comparing). */
#include "arklite.h"

#define ONrGameState_MotionBlur_Display  ((void (*)(void))0x4fcd00)
#define ONcMotionBlur_DisplayCall        0x4fb927
#define M3rDraw_State_SetInt             ((void (ONI_FC *)(u32, u32))0x41e170)
#define M3rDraw_State_GetInt             ((u32 (ONI_FC *)(u32))0x41e1a0)
#define M3cDrawStateIntType_ZWrite       4
#define M3cDrawState_ZWrite_Off          0

static void motion_blur_display(void)
{
	u32 zwrite = M3rDraw_State_GetInt(M3cDrawStateIntType_ZWrite);
	M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_Off);
	ONrGameState_MotionBlur_Display();
	M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, zwrite);
}

void motion_blur_install(void)
{
	char v[8];
	if (GetEnvironmentVariableA("ARKLITE_MOTION_BLUR_FIX", v, sizeof v) && v[0] == '0') {
		log_msg("motion blur: left as Oni draws it (ARKLITE_MOTION_BLUR_FIX=0)");
		return;
	}
	if (patch_call(ONcMotionBlur_DisplayCall, 0x4fcd00, (u32)motion_blur_display))
		log_msg("motion blur: afterimages drawn without depth writes (they no longer hide what's behind them)");
	else
		log_msg("motion blur: the call at 0x%x changed, left alone", ONcMotionBlur_DisplayCall);
}
