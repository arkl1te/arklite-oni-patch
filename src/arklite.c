/* Arklite: small quality-of-life patches for Oni (Anniversary Edition).
 *
 * Loaded by Arklite's loader (loader.c, built as realbink.dll) with LoadLibraryA("Arklite.dll").
 * Addresses are for the original Oni.exe (sha256 363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d).
 *
 * Patches:
 *   - Custom Game (custom_game.c): a main-menu button that lists and starts the levels in
 *     GameDataFolder\CustomLevels and any installed non-original level.
 *   - Pause menu music (Options > More... > When Game Pauses; more_options.c). Pressing
 *     Escape during a level opens the main menu dialog (0x96). Its init handler (0x4d2cd0,
 *     msg 0x3d) starts "main_menu_win" at full volume and then fades it to 1.0 over 0.5 s
 *     (call at 0x4d2d6a). Unless the setting is "Play Main Theme" we turn that fade into
 *     "volume 0, now" while a level is loaded. The music slot is still started and stopped
 *     exactly as before, so the level's own sounds resume as usual; the title screen keeps
 *     its music.
 *     The init handler also stops every playing channel (call at 0x4d2d4f, to 0x4835d0);
 *     closing the dialog plays them again (0x483660). With "Keep playing level music" we stop
 *     every channel except those of a Music play started by sound_music_start (tagged in
 *     volume.c), so the level's music goes on under the menu.
 *   - Pause menu background opacity (Options > More..., 33% by default). Before running the
 *     dialog, 0x4d2c70 sets the desktop background (desktop msg 0x3c) to partspec list entry
 *     1, InGameBackground = ps_black_none_none, which the desktop draws full screen over the
 *     level (the modal loop still renders the game through its draw hook, 0x4fb480). The
 *     desktop's paint handler draws a partspec background with the call at 0x476f0c (to
 *     0x4698b0, alpha 0xff). We route that call through a function that sets the alpha from
 *     the setting when the partspec is InGameBackground, so the paused game shows through.
 *   - HUD scale (hud_scale.c, set in Options > More...).
 *   - Sound, Music and Voice volume (volume.c): Options > Overall Volume becomes three sliders.
 *   - No dream-lab classes in random costumes (random_class.c): AE's shared level0 put the
 *     half-size striker_dream_lab in every level's striker_easy random pool.
 *   - 255 characters, all active at once (char_limit.c): the stock 64 active slots crash the
 *     game when more AIs than that go into combat.
 *   - Player 2 on a gamepad (players.c): multiplayer step 2.
 *   - Live pause menu (live_menu.c): in multiplayer games the game keeps running behind it.
 *   - Spawn points and respawning (players.c), item respawn (items.c): multiplayer maps.
 *   - Damage x1.25 and faster overhealing drain (mp_rules.c): multiplayer maps.
 *   - Input record and replay (lockstep.c): the determinism test for multiplayer.
 *   - Two PCs over UDP (net.c): multiplayer step 3, lockstep with input delay.
 *   - Crash log (crash_log.c): faults and the exit stack go to Arklite.log. */

#include "arklite.h"

/* ---- Engine ---------------------------------------------------------------------------- */

/* Current level number, 0 on the title screen (also decides whether "Resume" is shown) */
#define ONrLevel_GetCurrent ((u16 (__fastcall *)(void))0x5054c0)
/* Fade the playing music (handle in 0x61fa00) to volume over seconds; ret 8, ECX/EDX unused */
#define SSrMusic_Fade       ((void (__stdcall *)(float, float))0x512730)
#define ONcMainMenu_MusicFadeCall 0x4d2d6a
/* Stop every playing channel (both pools); the pause menu's init calls it at 0x4d2d4f */
#define SSrChannels_StopAll ((void (*)(void))0x4835d0)
#define ONcMainMenu_StopChannelsCall 0x4d2d4f
/* ECX channel: IDirectSoundBuffer::Stop */
#define SSrChannel_Stop     ((void (ONI_FC *)(u8 *))0x484620)
#define SSgChannels1        (*(u8 **)0x5eb74c)
#define SSgChannelCount1    (*(u32 *)0x63f390)
#define SSgChannels2        (*(u8 **)0x5eb750)
#define SSgChannelCount2    (*(u32 *)0x63f388)
#define SSgLock             (*(void **)0x63f394)
#define SSrLock             ((void (ONI_FC *)(void *))0x487070)
#define SSrUnlock           ((void (ONI_FC *)(void *))0x487060)
/* Partspec list entry by index (1 = InGameBackground, the pause menu's desktop background) */
#define PSrList_Get         ((void *(__fastcall *)(u32))0x46aa10)
/* Draw a partspec: ECX draw context, EDX partspec; alpha 0..255; ret 0x14 */
#define DCrDraw_PartSpec    ((void (__fastcall *)(void *, void *, u32, void *, u32, u32, u32))0x4698b0)
#define WMcDesktop_DrawBackgroundCall 0x476f0c

/* ---- Pause menu ------------------------------------------------------------------------ */

static void __stdcall main_menu_music_fade(float volume, float seconds)
{
	/* opened from a level: the pause menu (no theme over a game that keeps running) */
	if (ONrLevel_GetCurrent() && (pause_music_mode() != PAUSE_MUSIC_THEME || live_menu_open())) {
		volume = 0.0f;
		seconds = 0.0f;
	}
	SSrMusic_Fade(volume, seconds);
}

static void main_menu_stop_channels(void)
{
	u8 *pools[2] = { SSgChannels1, SSgChannels2 };
	u32 counts[2] = { SSgChannelCount1, SSgChannelCount2 };

	if (live_menu_open()) return;   /* the game goes on, and so do its sounds */
	/* title screen, or the level's music isn't kept: as before */
	if (!ONrLevel_GetCurrent() || pause_music_mode() != PAUSE_MUSIC_KEEP) {
		SSrChannels_StopAll();
		return;
	}
	SSrLock(SSgLock);
	for (int k = 0; k < 2; k++) {
		if (!pools[k]) continue;
		for (u32 i = 0; i < counts[k]; i++) {
			u8 *ch = pools[k] + i * 0x10c;
			if (!(ch[0x1b] & 1)) continue;   /* not playing */
			if (volume_channel_is_music(ch)) continue;
			SSrChannel_Stop(ch);
		}
	}
	SSrUnlock(SSgLock);
}

static void __fastcall desktop_draw_background(void *context, void *partspec, u32 flags,
	void *location, u32 width, u32 height, u32 alpha)
{
	if (partspec == PSrList_Get(1))   /* InGameBackground: the pause menu's black */
		alpha = pause_background_alpha();
	DCrDraw_PartSpec(context, partspec, flags, location, width, height, alpha);
}

#ifndef ARKLITE_VERSION
#define ARKLITE_VERSION "dev"
#endif

int WINAPI DllMain(void *instance, u32 reason, void *reserved)
{
	if (reason == 0 && reserved) {   /* DLL_PROCESS_DETACH, process exiting */
		lockstep_exit();
		net_exit();
		crash_log_exit();
		return 1;
	}
	if (reason != 1) return 1;   /* DLL_PROCESS_ATTACH */

	{
		/* a second Oni on this PC (ARKLITE_JOIN) can't share the first one's log */
		char log_name[MAX_PATH], v[4];
		if (!GetEnvironmentVariableA("ARKLITE_LOG", log_name, sizeof log_name) ||
		    str_len(log_name) >= sizeof log_name)
			str_cpy(log_name, sizeof log_name,
			        GetEnvironmentVariableA("ARKLITE_JOIN", v, sizeof v) ? "Arklite-join.log" : "Arklite.log");
		log_open(log_name);
	}
	log_msg("Arklite %s loaded", ARKLITE_VERSION);
	crash_log_install(instance);
	more_options_install();
	if (patch_call(ONcMainMenu_MusicFadeCall, (u32)SSrMusic_Fade, (u32)main_menu_music_fade))
		log_msg("pause menu music: patched");
	if (patch_call(ONcMainMenu_StopChannelsCall, (u32)SSrChannels_StopAll, (u32)main_menu_stop_channels))
		log_msg("pause menu level music: patched");
	if (patch_call(WMcDesktop_DrawBackgroundCall, (u32)DCrDraw_PartSpec, (u32)desktop_draw_background))
		log_msg("pause menu background: patched");
	custom_game_install();
	volume_install();
	hud_scale_install();
	random_class_install();
	char_limit_install();
	players_install();
	items_install();
	mp_rules_install();
	net_install();
	lockstep_install();
	live_menu_install();
	return 1;
}
