/* Sound, Music and Voice volume: replaces Options > Overall Volume with three sliders.
 *
 * Mixing. Every channel volume reaches DirectSound through SSrChannel_SetVolume (0x485480,
 * 21 call sites), which we route through a function that applies the slider of the category
 * of the play that owns the channel. Plays (0x58-byte structs at 0x5eb728) hold their channels at
 * +0xc/+0x10 and their play id at +0. Play ids are tagged where the engine starts them:
 *   - Music: the main menu music (0x512798) and sound_music_start (0x5144de, the handler it
 *     shares with sound_ambient_start, so we wrap the sound_music_start registration).
 *   - Voice: character dialog, sound_dialog_play* (0x5141f6).
 *   - Sound: everything else (impulse sounds, ambients, level sound objects).
 * A new play sets its first channel volume inside the start call, before it stores the
 * channel, so we also wrap the play update (0x480c20, 5 call sites) to know which play is
 * being updated.
 *
 * The persisted Overall Volume (persist.dat, getter 0x50f120) is no longer used: the sound
 * init that applied it to the master volume (0x51497f) gets 1.0, and Bink movies (15 reads
 * in 0x488000..0x489800) follow the Music volume. The master volume itself (0x5eb71c) is
 * left alone, since the engine uses it for its own fades.
 *
 * Options dialog (0x98, proc 0x4d25c0). The stock item 102 (Overall Volume) becomes Sound
 * Volume; Music (111) and Voice (112) are cloned from it below, and the three rows are
 * spread over the Sound box. Values (0..100) are saved in Arklite.ini, [volume]. */
#include "arklite.h"

enum { CAT_SOUND, CAT_MUSIC, CAT_VOICE, CAT_COUNT };

/* ---- Engine ---------------------------------------------------------------------------- */

#define SSgEnabled       (*(u8 *)0x5eb757)
#define SSgPlays         (*(u8 **)0x5eb728)
#define SSgPlayCount     (*(u32 *)0x5eb720)
#define SScPlaySize      0x58
#define SSmPlay_ID(p)    (*(u32 *)(p))
#define SSmPlay_State(p) (*(u32 *)((u8 *)(p) + 4))
#define SSmPlay_Chan1(p) (*(u8 **)((u8 *)(p) + 0xc))
#define SSmPlay_Chan2(p) (*(u8 **)((u8 *)(p) + 0x10))
/* Two channel pools, 0x10c bytes each; +0x3c = volume last computed by the engine */
#define SSgChannels1     (*(u8 **)0x5eb74c)
#define SSgChannelCount1 (*(u32 *)0x63f390)
#define SSgChannels2     (*(u8 **)0x5eb750)
#define SSgChannelCount2 (*(u32 *)0x63f388)
#define SScChannelSize   0x10c
#define SSmChannel_Volume(c) (*(float *)((u8 *)(c) + 0x3c))
#define SSgLock          (*(void **)0x63f394)
#define SSrLock          ((void (ONI_FC *)(void *))0x487070)
#define SSrUnlock        ((void (ONI_FC *)(void *))0x487060)

/* ECX channel, EDX unused, linear volume on the stack. It hands DirectSound
 * 3333 * volume - 3333 hundredths of a dB (truncated), or silence for volume <= 0.05. */
#define SSrChannel_SetVolume ((void (ONI_FC *)(u8 *, u32, float))0x485480)
#define SScChannel_MinVolume 0.05f
#define SSmChannel_Buffer(c) (*(void ***)((u8 *)(c) + 0x58))   /* IDirectSoundBuffer */
#define DSrBuffer_SetVolume(b) (*(long (__stdcall **)(void *, long))(*(u8 **)(b) + 0x3c))
#define DSBVOLUME_MIN        (-10000)
#define SSrPlay_Update       ((u32 (ONI_FC *)(u8 *, u32))0x480c20)
/* ECX ambient, EDX &volume; returns the play id or -1 */
#define SSrAmbient_Start2D   ((u32 (ONI_FC *)(void *, float *))0x4823a0)
/* Script handler shared by sound_music_start and sound_ambient_start (ret 0x10) */
#define SLrSound_AmbientStart ((u16 (ONI_FC *)(u32, u32, u32, u32, u32, u32))0x514410)
#define SLcMusicStartPushAddr 0x512e60
/* Persisted Overall Volume, 0..1 */
#define ONrPersist_GetOverallVolume ((float (ONI_FC *)(void))0x50f120)
#define ONgPersist_OverallVolume    (*(float *)0x5ed380)

static const u32 g_setVolumeCalls[] = {
	0x47fa49, 0x4806ae, 0x480813, 0x4810ab, 0x4810de, 0x48134f, 0x481368, 0x481418, 0x481431,
	0x481981, 0x4819e1, 0x4819ef, 0x482162, 0x4821cd, 0x4825d8, 0x4825e5, 0x48264d, 0x48357a,
	0x4835b5, 0x483d0a, 0x483d8d,
};
static const u32 g_playUpdateCalls[] = { 0x480c04, 0x481dfb, 0x48237d, 0x48379d, 0x483dc7 };
static const u32 g_movieVolumeCalls[] = {
	0x488125, 0x48813f, 0x488159, 0x48822e, 0x488248, 0x488262, 0x489340, 0x48935a, 0x489374,
	0x4896e7, 0x489701, 0x48971b, 0x4897a6, 0x4897c0, 0x4897da,
};
#define ONcSound_InitVolumeCall 0x51497f
#define ONcMenuMusic_StartCall  0x512798
#define ONcDialog_StartCall     0x5141f6
#define SLcAmbient_StartCall    0x5144de

/* Options dialog */
#define ONcOptions_Proc         0x4d25c0
#define ONcOptions_ProcPushAddr 0x4d2f26
#define ID_SOUND 102            /* the stock Overall Volume slider */
#define ID_MUSIC 111
#define ID_VOICE 112
#define cRowStep 25             /* row spacing in the Graphics box */

/* Slider class. Its proc sends mouse messages to 0x472db0 (ECX window, EDX msg, then the
 * global point packed x << 16 | y and the button flags). A press on the thumb starts a
 * drag; a press on the track pages by a tenth of the range, so 0..100 sliders could only
 * be clicked in steps of 10. We turn a press on the track of our sliders into a drag with
 * the thumb centred under the cursor. */
#define WMrSlider_Mouse       ((void (ONI_FC *)(WMtWindow *, u32, u32, u32))0x472db0)
#define WMcSlider_MouseCall   0x472b45
#define WMcMessage_MouseMove  0x1e
#define WMcMessage_MouseDown  0x1f
/* min, max, value, flags (bit 0 = pressed), dragging, grab x - thumb x, thumb x, thumb y, thumb width */
typedef struct { int min, max, value; u8 flags, dragging; short grab, thumb_x, thumb_y, thumb_w; } WMtSlider;
#define WMrWindow_GetData     ((WMtSlider *(ONI_FC *)(WMtWindow *, u32))0x474dc0)
#define WMrWindow_IsEnabled   ((u8 (ONI_FC *)(WMtWindow *))0x474d00)
#define WMrWindow_SetCapture  ((void (ONI_FC *)(WMtWindow *))0x474900)
/* global point -> window-local point */
#define WMrWindow_GlobalToLocal ((u8 (ONI_FC *)(WMtWindow *, const short *, short *))0x475060)
/* window, slider, part (4 = thumb), rect {left, top, right, bottom} */
#define WMrSlider_GetPartRect ((void (ONI_FC *)(WMtWindow *, WMtSlider *, u32, short *))0x4724c0)
#define WMrRect_Contains      ((u8 (ONI_FC *)(const short *, const short *))0x447ff0)

/* ---- Settings ------------------------------------------------------------------------------ */

static const char *const g_keys[CAT_COUNT] = { "sound", "music", "voice" };
static int  g_volume[CAT_COUNT] = { 100, 100, 100 };   /* 0..100 */
static int  g_loaded;

/* Runs on first use, after persist.dat is loaded: a missing key starts at the old Overall Volume. */
static void settings_load(void)
{
	float overall = ONgPersist_OverallVolume;
	int def;

	if (g_loaded) return;
	g_loaded = 1;
	if (!(overall >= 0.0f && overall <= 1.0f)) overall = 1.0f;
	def = (int)(overall * 100.0f + 0.5f);
	for (int c = 0; c < CAT_COUNT; c++) {
		int v = (int)GetPrivateProfileIntA("volume", g_keys[c], def, ini_path());
		g_volume[c] = v < 0 ? 0 : v > 100 ? 100 : v;
	}
	log_msg("volume: sound %d, music %d, voice %d", g_volume[CAT_SOUND], g_volume[CAT_MUSIC], g_volume[CAT_VOICE]);
}

static void settings_save(int c)
{
	char value[16];
	fmt(value, "%d", g_volume[c]);
	WritePrivateProfileStringA("volume", g_keys[c], value, ini_path());
}

/* Slider position -> attenuation in hundredths of a dB: 40 * log10(v / 100), i.e. the slider
 * sets amplitude (v / 100)^2. Half way is -12 dB, 10 is -40 dB, and it fades smoothly to
 * silence at 0. The slider used to scale the engine's linear volume, which the engine maps
 * to dB linearly and cuts to silence below 0.05: half way was already -17 dB and anything
 * under 5 was a sudden mute. */
static const short g_attenuation[101] = {
	-10000, -8000, -6796, -6092, -5592, -5204, -4887, -4620, -4388, -4183,
	-4000, -3834, -3683, -3544, -3415, -3296, -3184, -3078, -2979, -2885,
	-2796, -2711, -2630, -2553, -2479, -2408, -2340, -2275, -2211, -2150,
	-2092, -2035, -1979, -1926, -1874, -1824, -1775, -1727, -1681, -1636,
	-1592, -1549, -1507, -1466, -1426, -1387, -1349, -1312, -1275, -1239,
	-1204, -1170, -1136, -1103, -1070, -1039, -1007, -977, -946, -917,
	-887, -859, -830, -803, -775, -748, -722, -696, -670, -645,
	-620, -595, -571, -547, -523, -500, -477, -454, -432, -409,
	-388, -366, -345, -324, -303, -282, -262, -242, -222, -202,
	-183, -164, -145, -126, -107, -89, -71, -53, -35, -17,
	0,
};

/* The same curve as a linear gain, for Bink */
static float gain(int c) { return g_volume[c] * g_volume[c] * 0.0001f; }

/* ---- Categories ------------------------------------------------------------------------------ */

#define MAX_TAGS 64
static struct { u32 id; u8 cat; } g_tags[MAX_TAGS];   /* play ids are never reused (counter at 0x5eb70c) */
static u32 g_ntags;
static u8 *g_play;          /* play being updated by SSrPlay_Update */
static int g_starting;      /* category of the play being started, CAT_SOUND if none */
static int g_scriptMusic;   /* inside sound_music_start */

static void tag_play(u32 id, int cat)
{
	if (id == 0xffffffff || cat == CAT_SOUND) return;
	g_tags[g_ntags % MAX_TAGS].id = id;
	g_tags[g_ntags % MAX_TAGS].cat = (u8)cat;
	g_ntags++;
}

static int play_category(u8 *play)
{
	u32 id = SSmPlay_ID(play);
	for (u32 i = 0; i < MAX_TAGS && i < g_ntags; i++)
		if (g_tags[i].id == id) return g_tags[i].cat;
	return CAT_SOUND;
}

static int channel_category(u8 *channel)
{
	u8 *play = g_play;

	if (play && g_starting != CAT_SOUND) return g_starting;
	if (!play && SSgPlays) {
		for (u32 i = 0; i < SSgPlayCount; i++) {
			u8 *p = SSgPlays + i * SScPlaySize;
			if (SSmPlay_State(p) && (SSmPlay_Chan1(p) == channel || SSmPlay_Chan2(p) == channel)) {
				play = p;
				break;
			}
		}
	}
	return play ? play_category(play) : CAT_SOUND;
}

int volume_channel_is_music(u8 *channel) { return channel_category(channel) == CAT_MUSIC; }

/* ---- Hooks -------------------------------------------------------------------------------- */

/* The engine's own dB for the channel, plus the slider's attenuation */
static void ONI_FC channel_set_volume(u8 *channel, u32 unused, float volume)
{
	int v = g_volume[channel_category(channel)];
	long db;

	if (v >= 100) {
		SSrChannel_SetVolume(channel, unused, volume);
		return;
	}
	if (v <= 0 || !(volume > SScChannel_MinVolume)) {
		db = DSBVOLUME_MIN;
	} else {
		db = (long)(volume * 3333.0f) - 3333 + g_attenuation[v];
		if (db < DSBVOLUME_MIN) db = DSBVOLUME_MIN;
	}
	DSrBuffer_SetVolume(SSmChannel_Buffer(channel))(SSmChannel_Buffer(channel), db);
}

static u32 ONI_FC play_update(u8 *play, u32 edx)
{
	u8 *outer = g_play;
	u32 result;

	g_play = play;
	result = SSrPlay_Update(play, edx);
	g_play = outer;
	return result;
}

static u32 start_tagged(void *ambient, float *volume, int cat)
{
	u32 id;

	g_starting = cat;
	id = SSrAmbient_Start2D(ambient, volume);
	g_starting = CAT_SOUND;
	tag_play(id, cat);
	return id;
}

static u32 ONI_FC menu_music_start(void *ambient, float *volume) { return start_tagged(ambient, volume, CAT_MUSIC); }
static u32 ONI_FC dialog_start(void *ambient, float *volume)     { return start_tagged(ambient, volume, CAT_VOICE); }
static u32 ONI_FC script_ambient_start(void *ambient, float *volume)
{
	return start_tagged(ambient, volume, g_scriptMusic ? CAT_MUSIC : CAT_SOUND);
}

static u16 ONI_FC sound_music_start(u32 ecx, u32 edx, u32 a, u32 b, u32 c, u32 d)
{
	u16 result;

	g_scriptMusic = 1;
	result = SLrSound_AmbientStart(ecx, edx, a, b, c, d);
	g_scriptMusic = 0;
	return result;
}

static float ONI_FC sound_init_volume(void)
{
	settings_load();
	return 1.0f;
}

static float ONI_FC movie_volume(void)
{
	settings_load();
	return gain(CAT_MUSIC);
}

/* Re-send every channel's volume, so a slider change is heard at once. */
static void refresh_channels(void)
{
	u8 *pools[2] = { SSgChannels1, SSgChannels2 };
	u32 counts[2] = { SSgChannelCount1, SSgChannelCount2 };

	if (!SSgEnabled) return;
	SSrLock(SSgLock);
	for (int k = 0; k < 2; k++) {
		if (!pools[k]) continue;
		for (u32 i = 0; i < counts[k]; i++) {
			u8 *ch = pools[k] + i * SScChannelSize;
			channel_set_volume(ch, 0, SSmChannel_Volume(ch));
		}
	}
	SSrUnlock(SSgLock);
}

/* ---- Options dialog -------------------------------------------------------------------------- */

static int g_initializing;
static WMtWindow *g_sliders[CAT_COUNT];   /* in the open (or last) Options dialog */

static void ONI_FC slider_mouse(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	short global[2] = { (short)(p1 >> 16), (short)p1 }, local[2], thumb[4];
	WMtSlider *s;
	int ours = 0;

	for (int c = 0; c < CAT_COUNT; c++) ours |= w == g_sliders[c];
	if (msg != WMcMessage_MouseDown || !ours || !WMrWindow_IsEnabled(w) || !(s = WMrWindow_GetData(w, 0))
	    || !WMrWindow_GlobalToLocal(w, global, local)) {
		WMrSlider_Mouse(w, msg, p1, p2);
		return;
	}
	WMrSlider_GetPartRect(w, s, 4, thumb);
	if (s->dragging || WMrRect_Contains(thumb, local)) {
		WMrSlider_Mouse(w, msg, p1, p2);
		return;
	}
	/* What the stock press does, then a drag step to the cursor (it notifies the dialog) */
	s->flags |= 1;
	WMrWindow_SetFocus(w);
	WMrWindow_SetCapture(w);
	s->dragging = 1;
	s->grab = (short)-(s->thumb_w / 2);
	WMrSlider_Mouse(w, WMcMessage_MouseMove, p1, 1);
}

/* The label to the left of a control on the same row */
static WMtWindow *row_label(WMtWindow *dialog, WMtWindow *control)
{
	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w))
		if (w != control && WIN_ID(w) == 0 && WIN_Y(w) == WIN_Y(control) && WIN_X(w) + WIN_W(w) <= WIN_X(control))
			return w;
	return 0;
}

static WMtWindow *clone_window(WMtWindow *src, const char *title, u32 id, int y, WMtWindow *dialog)
{
	WMtWindow *w = WMrWindow_New(WIN_CLASS(src), title, WIN_FLAGS(src), WIN_STYLE(src), id,
	                             WIN_X(src), y, WIN_W(src), WIN_H(src), dialog, 0);
	if (!w) return 0;
	WMrWindow_SetFontInfo(w, WIN_FONTINFO(src));
	win_insert_after(w, src);
	return w;
}

static void add_volume_rows(WMtWindow *dialog)
{
	WMtWindow *sound = WMrDialog_GetItemByID(dialog, ID_SOUND);
	WMtWindow *label = sound ? row_label(dialog, sound) : 0;
	WMtWindow *music, *voice;
	int y;

	if (!sound || WMrDialog_GetItemByID(dialog, ID_MUSIC)) return;
	settings_load();

	/* Sound one row up, Music where Overall Volume was, Voice one row down. */
	y = WIN_Y(sound);
	music = clone_window(sound, "music", ID_MUSIC, y, dialog);
	voice = clone_window(sound, "voice", ID_VOICE, y + cRowStep, dialog);
	if (label) {
		clone_window(label, "Music Volume:", 0, y, dialog);
		clone_window(label, "Voice Volume:", 0, y + cRowStep, dialog);
		WMrWindow_SetTitle(label, "Sound Volume:", 0x100);
		WMrWindow_SetLocation(label, WIN_X(label), y - cRowStep);
	} else {
		log_msg("options: Overall Volume label not found");
	}
	WMrWindow_SetLocation(sound, WIN_X(sound), y - cRowStep);

	WMrSlider_SetValue(sound, g_volume[CAT_SOUND]);
	if (music) { WMrSlider_SetRange(music, 0, 100); WMrSlider_SetValue(music, g_volume[CAT_MUSIC]); }
	if (voice) { WMrSlider_SetRange(voice, 0, 100); WMrSlider_SetValue(voice, g_volume[CAT_VOICE]); }
	if (!music || !voice) log_msg("options: WMrWindow_New failed");
	g_sliders[CAT_SOUND] = sound;
	g_sliders[CAT_MUSIC] = music;
	g_sliders[CAT_VOICE] = voice;
}

static u8 ONI_FC options_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	WMtDialogProc original = (WMtDialogProc)ONcOptions_Proc;
	u32 id = p1 & 0xffff;
	u8 handled;

	/* Esc arrives as Close (id 2, click) with no window; a real Close click passes the button. */
	if (msg == WMcMessage_Command && id == 2 && (p1 >> 16) == WMcNotify_Click && !p2 && more_options_cancel(dialog))
		return 1;
	if (msg == WMcMessage_Command && more_options_command(dialog, id, p1 >> 16)) return 1;
	if (msg == WMcMessage_Command && (id == ID_SOUND || id == ID_MUSIC || id == ID_VOICE)) {
		int c = id == ID_SOUND ? CAT_SOUND : id == ID_MUSIC ? CAT_MUSIC : CAT_VOICE;
		WMtWindow *slider = WMrDialog_GetItemByID(dialog, id);

		if ((p1 >> 16) == 1 && slider && !g_initializing) {
			int v = WMrSlider_GetValue(slider);
			if (v != g_volume[c]) {
				g_volume[c] = v < 0 ? 0 : v > 100 ? 100 : v;
				settings_save(c);
				refresh_channels();
			}
		}
		return 1;   /* never the stock handler, which writes Overall Volume to persist.dat */
	}

	g_initializing = msg == WMcMessage_InitDialog;
	handled = original(dialog, msg, p1, p2);
	if (msg == WMcMessage_InitDialog) {
		add_volume_rows(dialog);
		more_options_init(dialog);
	}
	g_initializing = 0;
	return handled;
}

/* ---- Install ------------------------------------------------------------------------------- */

void volume_install(void)
{
	int n = 0, total = 0;

	for (unsigned i = 0; i < sizeof g_setVolumeCalls / sizeof g_setVolumeCalls[0]; i++, total++)
		n += patch_call(g_setVolumeCalls[i], (u32)SSrChannel_SetVolume, (u32)channel_set_volume);
	for (unsigned i = 0; i < sizeof g_playUpdateCalls / sizeof g_playUpdateCalls[0]; i++, total++)
		n += patch_call(g_playUpdateCalls[i], (u32)SSrPlay_Update, (u32)play_update);
	for (unsigned i = 0; i < sizeof g_movieVolumeCalls / sizeof g_movieVolumeCalls[0]; i++, total++)
		n += patch_call(g_movieVolumeCalls[i], (u32)ONrPersist_GetOverallVolume, (u32)movie_volume);
	n += patch_call(ONcSound_InitVolumeCall, (u32)ONrPersist_GetOverallVolume, (u32)sound_init_volume);
	n += patch_call(ONcMenuMusic_StartCall, (u32)SSrAmbient_Start2D, (u32)menu_music_start);
	n += patch_call(ONcDialog_StartCall, (u32)SSrAmbient_Start2D, (u32)dialog_start);
	n += patch_call(SLcAmbient_StartCall, (u32)SSrAmbient_Start2D, (u32)script_ambient_start);
	n += patch_push_imm32(SLcMusicStartPushAddr, (u32)SLrSound_AmbientStart, (u32)sound_music_start);
	n += patch_push_imm32(ONcOptions_ProcPushAddr, ONcOptions_Proc, (u32)options_proc);
	n += patch_call(WMcSlider_MouseCall, (u32)WMrSlider_Mouse, (u32)slider_mouse);
	total += 7;
	log_msg("volume: %d of %d patches applied (settings in %s)", n, total, ini_path());
}
