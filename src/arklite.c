/* Arklite: small quality-of-life patches for Oni (Anniversary Edition).
 *
 * Loaded by Arklite's loader (loader.c, built as realbink.dll) with LoadLibraryA("Arklite.dll").
 * Addresses are for the original Oni.exe (sha256 363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d).
 *
 * Patches:
 *   - Custom Game (custom_game.c, mp_menu.c): a main-menu button that lists and starts the levels in
 *     GameDataFolder\CustomLevels and any installed non-original level, alone or as a network
 *     host, and joins network games from a list of saved servers.
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
 *     Quit, New Game and Load Game fade it out over 1.5 s before leaving (exit_music_fade), where
 *     the engine only faded the menu's own music slot and the level's stopped dead.
 *   - Pause menu background opacity (Options > More..., 33% by default). Before running the
 *     dialog, 0x4d2c70 sets the desktop background (desktop msg 0x3c) to partspec list entry
 *     1, InGameBackground = ps_black_none_none, which the desktop draws full screen over the
 *     level (the modal loop still renders the game through its draw hook, 0x4fb480). The
 *     desktop's paint handler draws a partspec background with the call at 0x476f0c (to
 *     0x4698b0, alpha 0xff). We route that call through a function that sets the alpha from
 *     the setting when the partspec is InGameBackground, so the paused game shows through.
 *   - HUD scale and HUD Centering (hud_scale.c, set in Options > More...): the rings from the
 *     corners to side by side in the middle, the multiplayer corner text with them.
 *   - Sound, Music and Voice volume (volume.c): Options > Overall Volume becomes three sliders.
 *   - No dream-lab classes in random costumes (random_class.c): AE's shared level0 put the
 *     half-size striker_dream_lab in every level's striker_easy random pool.
 *   - No skybox seams (texture_clamp.c): clamped textures use GL_CLAMP_TO_EDGE, not GL_CLAMP,
 *     whose black border lined every edge of the skybox cube. Textures can be 1024x1024: the
 *     upload's conversion buffer stopped at 256x256. On wide screens the sky box grows enough
 *     that the near clip (really at depth ~4, not 2) no longer cuts black wedges from its corners.
 *   - Levels can add particle classes (particle_table.c): stock Oni never set up any class but
 *     level 0's, and its lookups ran off the sorted table's end, so such a level crashed loading.
 *   - 255 characters, all active at once (char_limit.c): the stock 64 active slots crash the
 *     game when more AIs than that go into combat.
 *   - Player 2 on a gamepad (players.c): multiplayer step 2.
 *   - Live pause menu (live_menu.c): in multiplayer games the game keeps running behind it.
 *   - Spawn points and respawning (players.c), item respawn (items.c): multiplayer maps.
 *   - Damage x1.25 and faster overhealing drain (mp_rules.c): multiplayer maps.
 *   - Crouched weapons aim as low as the body bends, Ninjabot aims its weapons (mp_rules.c):
 *     multiplayer maps.
 *   - Input record and replay (lockstep.c): the determinism test for multiplayer.
 *   - Two PCs over UDP (net.c): multiplayer step 3, lockstep with input delay; a lobby where F3
 *     readies up, a 7 s countdown, then the match. On-screen text and score (mp_hud.c).
 *   - Flickering and pulsing vertex lights in custom levels (vertex_light.c): Catwalk's
 *     vertexlight.bin, applied to the level's quad colors every frame; their sounds, and a
 *     flickering light's ambient Sound object playing only while the flicker fails.
 *   - Particles that turn with Catwalk's spinning groups (spin_particle.c): spinparticles.bin.
 *   - Crash log (crash_log.c): faults and the exit stack go to Arklite.log.
 *   - Problem reports (report.c): a desync's, a dropped player's or a crash's log and dumps to the
 *     Arklite helper, without addresses, chat or absolute paths ([reports] send=0: off).
 *   - Lens flares without stalls (flare_depth.c): on NVIDIA every flare read the depth buffer on its
 *     own, waiting for the GPU each time (Muro's combo: 170 FPS to 15); now one read a frame.
 *   - Photo mode (photo.c): F10 in single player stops the game, frees the camera (free fly or
 *     orbiting a character) and opens a settings panel on Tab; leaving puts everything back. */

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
/* The menu's music slot (play id, -1 = none) and its calls */
#define OSgMusic_PlayID     (*(u32 *)0x61fa00)
#define OSrMusic_IsPlaying  ((u8 (*)(void))0x512720)
#define OSrMusic_Stop       ((void (*)(void))0x5127b0)
#define OSrMusic_Halt       ((void (*)(void))0x512700)
#define SSrAmbient_Halt     ((void (ONI_FC *)(u32))0x481d90)
#define UUrMachineTime_Sixtieths ((u32 (*)(void))0x4263e0)
/* ECX listener position, EDX facing */
#define OSrUpdate           ((void (ONI_FC *)(float *, float *))0x514f20)
#define SS2rUpdate          ((void (*)(void))0x483bd0)
/* The menu's exits test OSrMusic_IsPlaying, then fade the slot over 0.5 s, updating sound only:
 * Load Game (0x4d2e64), Quit (0x4d2f8a), New Game (0x4d3100's, called at 0x4d2df1) */
static const u32 g_exitMusicCalls[] = { 0x4d2e64, 0x4d2f8a, 0x4d3103 };
#define cExitFadeSeconds 1.5f   /* the level's music; the menu's own theme keeps its 0.5 s */
#define cExitFadeDb      5000   /* hundredths of a dB: inaudible by the end */

/* ---- Pause menu ------------------------------------------------------------------------ */

/* Leaving the level from the menu (Quit, New Game, Load Game): the level's music kept playing under
 * it (Keep playing level music, or a live menu) stopped dead, since the engine's wait only faded
 * the menu's own slot. Now it fades out evenly, 50 dB over cExitFadeSeconds, then stops. Without
 * level music the engine's own code runs as before. Returns "menu music not playing" after doing
 * the caller's fade itself, so the caller skips its own. */
static u8 exit_music_fade(void)
{
	u32 ids[8], n = volume_music_playing(ids, 8, OSgMusic_PlayID), start, now, total;
	float position[3] = { 0 }, facing[3] = { 0 };

	if (!n) return OSrMusic_IsPlaying();
	SSrMusic_Fade(0.0f, 0.5f);   /* the menu's slot, as the caller would */
	OSrMusic_Stop();
	start = UUrMachineTime_Sixtieths();
	total = (u32)(cExitFadeSeconds * 60.0f);
	do {
		now = UUrMachineTime_Sixtieths() - start;
		volume_music_fade(-(long)(cExitFadeDb * (now < total ? now : total) / total));
		OSrUpdate(position, facing);
		SS2rUpdate();
	} while (now < total);
	OSrMusic_Halt();
	for (u32 i = 0; i < n; i++) SSrAmbient_Halt(ids[i]);
	volume_music_fade(0);   /* the next level's music (New Game, Load Game) plays as usual */
	log_msg("level music: faded out %u play(s) on leaving the level", n);
	return 0;
}

static void __stdcall main_menu_music_fade(float volume, float seconds)
{
	/* opened from a level: the pause menu (no theme over a game that keeps running) */
	if (ONrLevel_GetCurrent() && (pause_music_mode() != PAUSE_MUSIC_THEME || live_menu_open())) {
		volume = 0.0f;
		seconds = 0.0f;
	}
	SSrMusic_Fade(volume, seconds);
}

void sound_pause_channels(int keep_music)
{
	u8 *pools[2] = { SSgChannels1, SSgChannels2 };
	u32 counts[2] = { SSgChannelCount1, SSgChannelCount2 };

	if (!keep_music) {
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

static void main_menu_stop_channels(void)
{
	if (live_menu_open()) return;   /* the game goes on, and so do its sounds */
	/* title screen, or the level's music isn't kept: all of them, as before */
	sound_pause_channels(ONrLevel_GetCurrent() && pause_music_mode() == PAUSE_MUSIC_KEEP);
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

/* The CPU, for desyncs: CPU makers' x87 trig instructions can round differently, so two PCs'
 * logs of one desync tell whether their CPUs differ (cpuid: vendor, brand string, family/model) */
static void cpuid(u32 leaf, u32 r[4])
{
	__asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}

static void log_cpu(void)
{
	u32 r[4], max, vendor[4] = { 0 }, brand[13] = { 0 };
	char *b = (char *)brand;
	cpuid(0, r);
	vendor[0] = r[1], vendor[1] = r[3], vendor[2] = r[2];
	cpuid(1, r);
	u32 sig = r[0], base = sig >> 8 & 15;
	u32 family = base + (base == 15 ? (sig >> 20 & 255) : 0);
	u32 model = (sig >> 4 & 15) | (base >= 6 ? (sig >> 12 & 0xf0) : 0);
	cpuid(0x80000000u, r);
	max = r[0];
	for (u32 i = 0; i < 3 && max >= 0x80000004u; i++) cpuid(0x80000002u + i, brand + i * 4);
	while (*b == ' ') b++;
	for (int i = (int)str_len(b) - 1; i >= 0 && b[i] == ' '; i--) b[i] = 0;   /* (the brand is padded with spaces) */
	log_msg("cpu: %s (%s, family %d model %d stepping %d)", *b ? b : "?", (char *)vendor, family, model, sig & 15);
}

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
		/* a second Oni on this PC (ARKLITE_JOIN, a dedicated server) gets a log of its own name */
		char log_name[MAX_PATH], v[4];
		if (!GetEnvironmentVariableA("ARKLITE_LOG", log_name, sizeof log_name) ||
		    str_len(log_name) >= sizeof log_name)
			str_cpy(log_name, sizeof log_name,
			        GetEnvironmentVariableA("ARKLITE_DEDICATED", v, sizeof v) ? "Arklite-server.log" :
			        GetEnvironmentVariableA("ARKLITE_JOIN", v, sizeof v) ? "Arklite-join.log" : "Arklite.log");
		log_open(log_name);
	}
	log_msg("Arklite %s loaded", ARKLITE_VERSION);
	log_cpu();
	crash_log_install(instance);
	heap_check_install();   /* before rollback_install: rollback.c's heap hooks go through it */
	more_options_install();
	if (patch_call(ONcMainMenu_MusicFadeCall, (u32)SSrMusic_Fade, (u32)main_menu_music_fade))
		log_msg("pause menu music: patched");
	if (patch_call(ONcMainMenu_StopChannelsCall, (u32)SSrChannels_StopAll, (u32)main_menu_stop_channels))
		log_msg("pause menu level music: patched");
	if (patch_call(WMcDesktop_DrawBackgroundCall, (u32)DCrDraw_PartSpec, (u32)desktop_draw_background))
		log_msg("pause menu background: patched");
	{
		u32 ok = 0;
		for (u32 i = 0; i < sizeof g_exitMusicCalls / sizeof g_exitMusicCalls[0]; i++)
			ok += patch_call(g_exitMusicCalls[i], (u32)OSrMusic_IsPlaying, (u32)exit_music_fade) ? 1 : 0;
		log_msg("level music fade on exit: %u of 3 patched", ok);
	}
	custom_game_install();
	volume_install();
	hud_scale_install();
	random_class_install();
	texture_clamp_install();
	particle_table_install();
	vertex_light_install();
	respawn_fx_install();
	char_limit_install();
	players_install();
	items_install();
	mp_rules_install();
	mutators_install();
	obituary_install();
	regen_install();
	level0_swap_install();
	server_install();
	net_install();
	mp_hud_install();
	lockstep_install();
	rollback_install();
	live_menu_install();
	char_menu_install();
	mp_names_install();
	chat_install();
	text_install();
	mp_board_install();
	menu_wheel_install();
	screenshot_install();
	flare_depth_install();
	invis_install();
	motion_blur_install();
	sprite_aspect_install();
	photo_install();
	report_install(instance);
	return 1;
}
