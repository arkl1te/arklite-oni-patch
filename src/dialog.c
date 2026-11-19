/* Arklite's dialogue commands for a level's scripts, beside the game's sound_dialog_play,
 * _block and _interrupt:
 *
 *   ark_sound_dialog_play_interrupt name:string [immediate:bool]
 *       sound_dialog_play_interrupt that also takes the dialogue subtitles off the screen.
 *   ark_sound_dialog_stop_all [immediate:bool]
 *       stops every line of dialogue playing and takes their subtitles off the screen.
 *   ark_sound_dialog_play_overlap name:string
 *       starts a line while another plays, both heard and both subtitles shown.
 *
 * Subtitles go with a fade (the 64-tick one message_remove gives a line), or with immediate true
 * that frame. Without Arklite the game doesn't know these commands: the script stops there.
 *
 * The game's dialogue (Oni_Sound2.c): one line at a time. OSiDialog_Start 0x514160 (ECX the name,
 * DL interrupt) starts it: when a line plays (0x6200a8) it stalls, or with interrupt halts it
 * first (0x51417a: SSrAmbient_Halt 0x481d90, ECX the play id); then 0x6200a8 = 1, the play id at
 * 0x61fa30, and 0x620098 the tick its time is up (0: its sound decides). The sound update clears
 * the three when the sound ends or the time is up; sound_dialog_play_block waits for that.
 * The commands' callbacks (fastcall: ECX error context, EDX count, then params, ticks, stall,
 * return; ret 0x10): sound_dialog_play 0x514140 (DL 0, its stall ignored), _interrupt 0x514300.
 *
 * Overlap: the line playing is moved aside for a moment (0x6200a8 = 0) so that the game's own
 * sound_dialog_play starts the new one. Of the two, the one whose time is up later stays the
 * game's line (so _block waits for both); the other goes to g_extra. The halt in OSiDialog_Start
 * is wrapped: an interrupt (the game's or Arklite's) stops the g_extra lines too. Play ids only
 * count up (SS2's SSgPlayID), so halting an id that already ended does nothing.
 *
 * Which subtitles: OSiDialog_Start shows a line's subtitle with one of two calls, here wrapped to
 * keep a copy of its text (the last RecentTexts):
 *   0x5142b0 call COrConsole_PrintIdentified 0x430480 (letterbox up, cutscenes): the console
 *            lines (*0x5cb468), priority 1 (subtitle), no identifier;
 *   0x5142c3 call COrInGameSubtitle_Print 0x4304e0 (in game): the subtitle area (*0x5cb470),
 *            priority 2, no identifier.
 * A text area (COtTextArea): +0x1a entries (u16), +0x20 the entries, 0x214 bytes each (COtTextEntry):
 * +0 priority, +4 timer (ticks left, 0 = stays; the last 64 fade: alpha = timer * 4), +0x12 the
 * text (502 bytes), +0x208 identifier. COrTextArea_Print 0x431340 splits a text of more than 501
 * characters into entries of 501, skipping one character between them (Bungie's off-by-one).
 * COrTextArea_RemoveEntry 0x431760 (ECX area, EDX index) takes one out.
 *
 * The other entries in those areas (message lines from the subtitle file, which have an
 * identifier; the console; other subtitles) aren't touched: an entry goes only when its priority
 * is the dialogue's, it has no identifier and its text is a dialogue line's subtitle. */
#include "arklite.h"

typedef struct { u32 type; u32 val; } SLtParameter_Actual;
typedef u16 (ONI_FC *SLtCommand)(void *, u32, SLtParameter_Actual *, u32 *, u8 *, SLtParameter_Actual *);

#define SLrScript_Command_Register_Void ((u16 (ONI_FC *)(const char *, const char *, const char *, void *))0x477b40)
#define OSiDialog_Play                  ((SLtCommand)0x514140)
#define OSiDialog_Play_Interrupted      ((SLtCommand)0x514300)
#define SSrAmbient_Halt                 ((void (ONI_FC *)(u32))0x481d90)
/* SSrAmbient_Update 0x4824a0 (ECX play id, EDX position, then direction, velocity, volume): with
 * no position etc. it only answers whether the sound still plays, as the dialogue update asks */
#define SSrAmbient_Update               ((u8 (ONI_FC *)(u32, void *, void *, void *, void *))0x4824a0)
#define COrConsole_PrintIdentified      ((void (ONI_FC *)(u32, u32, u32, const char *, const char *, u32))0x430480)
#define COrInGameSubtitle_Print         ((void (ONI_FC *)(const char *, const char *, u32))0x4304e0)
#define COrTextArea_RemoveEntry         ((void (ONI_FC *)(u8 *, u32))0x431760)
#define OSgDialog_Playing               (*(u8 *)0x6200a8)
#define OSgDialog_PlayID                (*(u32 *)0x61fa30)
#define OSgDialog_EndTime               (*(u32 *)0x620098)
#define COgConsoleLines                 (*(u8 **)0x5cb468)
#define COgInGameSubtitleArea           (*(u8 **)0x5cb470)
#define OSiDialog_Start_HaltCall        0x51417a
#define OSiDialog_Start_CutsceneCall    0x5142b0
#define OSiDialog_Start_InGameCall      0x5142c3
#define SScInvalidID                    0xffffffffu

#define EntrySize        0x214
#define EntryText        0x12
#define EntryTextLen     0x1f6   /* the buffer; a line holds 501 characters */
#define EntryIdentifier  0x208
#define FadeTicks        64      /* COcImmediateFadeTime */
#define RecentTexts      16
#define MaxExtra         256     /* more than SS2 can play at once (one per sound channel) */

static char g_texts[RecentTexts][4096];   /* the latest dialogue lines' subtitles ("" none) */
static u32 g_next_text;
static u32 g_extra[MaxExtra];              /* play ids of lines overlapping the game's line */
static u32 g_nextra;

static void keep(const char *text)
{
	char *d = g_texts[g_next_text++ % RecentTexts];
	u32 i = 0;
	if (text)
		for (; text[i] && i < sizeof g_texts[0] - 1; i++) d[i] = text[i];
	d[i] = 0;
}

static void ONI_FC cutscene_print(u32 priority, u32 shade, u32 shadow, const char *text, const char *id, u32 ticks)
{
	keep(text);
	COrConsole_PrintIdentified(priority, shade, shadow, text, id, ticks);
}

static void ONI_FC ingame_print(const char *text, const char *id, u32 ticks)
{
	keep(text);
	COrInGameSubtitle_Print(text, id, ticks);
}

static void halt_extra(void)
{
	for (u32 i = 0; i < g_nextra; i++) SSrAmbient_Halt(g_extra[i]);
	g_nextra = 0;
}

/* OSiDialog_Start's halt (an interrupt): the overlapping lines stop with the game's */
static void ONI_FC halt_dialog(u32 play_id)
{
	SSrAmbient_Halt(play_id);
	halt_extra();
}

static int plays(u32 play_id)
{
	return play_id != SScInvalidID && SSrAmbient_Update(play_id, 0, 0, 0, 0) != 0;
}

/* The overlapping lines that ended by themselves leave the list */
static void prune_extra(void)
{
	u32 n = 0;
	for (u32 i = 0; i < g_nextra; i++)
		if (plays(g_extra[i])) g_extra[n++] = g_extra[i];
	g_nextra = n;
}

static void add_extra(u32 play_id)
{
	if (play_id == SScInvalidID) return;
	prune_extra();
	if (g_nextra == MaxExtra) {   /* can't happen: the list holds only lines still playing */
		log_msg("dialog: %u overlapping lines playing, the oldest no longer tracked", g_nextra);
		for (u32 i = 1; i < MaxExtra; i++) g_extra[i - 1] = g_extra[i];
		g_nextra--;
	}
	g_extra[g_nextra++] = play_id;
}

/* The entry's text is a piece of text as COrTextArea_Print cuts it */
static int is_piece(const char *t, const char *text)
{
	u32 len = 0, n = 0;
	while (text[len]) len++;
	while (n < EntryTextLen && t[n]) n++;
	for (u32 off = 0; off < len; off += EntryTextLen) {
		u32 rest = len - off, want = rest > EntryTextLen - 1 ? EntryTextLen - 1 : rest;
		if (n != want) continue;
		u32 i = 0;
		while (i < n && t[i] == text[off + i]) i++;
		if (i == n) return 1;
	}
	return 0;
}

static int is_dialogue(const char *t)
{
	for (u32 k = 0; k < RecentTexts; k++)
		if (g_texts[k][0] && is_piece(t, g_texts[k])) return 1;
	return 0;
}

static int hide_in(u8 *area, u32 priority, int immediate)
{
	if (!area) return 0;
	int hidden = 0;
	for (int i = (int)*(u16 *)(area + 0x1a) - 1; i >= 0; i--) {
		u8 *e = *(u8 **)(area + 0x20) + i * EntrySize;
		if (*(u32 *)e != priority || *(const char **)(e + EntryIdentifier) != 0) continue;
		if (!is_dialogue((const char *)(e + EntryText))) continue;
		if (immediate) COrTextArea_RemoveEntry(area, (u32)i);
		else {
			u32 *timer = (u32 *)(e + 4);
			if (*timer == 0 || *timer > FadeTicks) *timer = FadeTicks;
		}
		hidden++;
	}
	return hidden;
}

/* Every dialogue subtitle on screen: in game and in cutscenes */
static int hide_subtitles(int immediate)
{
	return hide_in(COgInGameSubtitleArea, 2, immediate) + hide_in(COgConsoleLines, 1, immediate);
}

static u16 ONI_FC play_interrupt(void *where, u32 count, SLtParameter_Actual *params, u32 *ticks, u8 *stall,
                                  SLtParameter_Actual *ret)
{
	int immediate = count >= 2 && (params[1].val & 0xff) != 0;
	/* Before the new line starts: the same line again keeps its new subtitle */
	int hidden = hide_subtitles(immediate);
	log_msg("dialog: ark_sound_dialog_play_interrupt %s%s: %d subtitle line(s) %s", (const char *)params[0].val,
	        immediate ? " immediate" : "", hidden, immediate ? "removed" : "fading");
	return OSiDialog_Play_Interrupted(where, count, params, ticks, stall, ret);
}

static u16 ONI_FC stop_all(void *where, u32 count, SLtParameter_Actual *params, u32 *ticks, u8 *stall,
                            SLtParameter_Actual *ret)
{
	int immediate = count >= 1 && (params[0].val & 0xff) != 0;
	prune_extra();
	int lines = (OSgDialog_Playing && plays(OSgDialog_PlayID)) + (int)g_nextra;
	if (OSgDialog_Playing) SSrAmbient_Halt(OSgDialog_PlayID);
	halt_extra();
	/* As an interrupt leaves it: nothing playing, a _block waiting goes on */
	OSgDialog_Playing = 0;
	OSgDialog_PlayID = SScInvalidID;
	OSgDialog_EndTime = 0;
	int hidden = hide_subtitles(immediate);
	log_msg("dialog: ark_sound_dialog_stop_all%s: %d line(s) stopped, %d subtitle line(s) %s",
	        immediate ? " immediate" : "", lines, hidden, immediate ? "removed" : "fading");
	return 0;
}

static u16 ONI_FC play_overlap(void *where, u32 count, SLtParameter_Actual *params, u32 *ticks, u8 *stall,
                                SLtParameter_Actual *ret)
{
	const char *name = (const char *)params[0].val;
	u8 was = OSgDialog_Playing;
	u32 old_id = OSgDialog_PlayID, old_end = OSgDialog_EndTime;
	OSgDialog_Playing = 0;   /* so the game starts the new line instead of waiting */
	u16 r = OSiDialog_Play(where, count, params, ticks, stall, ret);
	if (!was) {
		log_msg("dialog: ark_sound_dialog_play_overlap %s: nothing else playing", name);
		return r;
	}
	if (!OSgDialog_Playing) {
		/* The new line didn't start (no such sound): the old one is the game's line again */
		OSgDialog_Playing = 1;
		OSgDialog_PlayID = old_id;
		OSgDialog_EndTime = old_end;
		log_msg("dialog: ark_sound_dialog_play_overlap %s: didn't start", name);
		return r;
	}
	/* The game's line is the one whose time is up later (0: when its sound ends, any time) */
	u32 new_id = OSgDialog_PlayID, new_end = OSgDialog_EndTime;
	int keep_old = new_end != 0 && (old_end == 0 || old_end > new_end);
	if (keep_old) {
		add_extra(new_id);
		OSgDialog_PlayID = old_id;
		OSgDialog_EndTime = old_end;
	} else add_extra(old_id);
	log_msg("dialog: ark_sound_dialog_play_overlap %s: over another line, %u playing, the game waits for the %s one",
	        name, g_nextra + 1, keep_old ? "older" : "new");
	return r;
}

static void reg(const char *name, const char *desc, const char *args, SLtCommand f)
{
	u16 r = SLrScript_Command_Register_Void(name, desc, args, (void *)f);
	if (r) log_msg("dialog: %s not registered (error %u)", name, r);
}

void dialog_bsl_register(void)
{
	reg("ark_sound_dialog_play_interrupt",
	    "interrupts the current character dialog and plays a new one, hiding the old line's subtitle "
	    "(immediate: without fading out)",
	    "name:string [immediate:bool | ]", play_interrupt);
	reg("ark_sound_dialog_stop_all",
	    "stops all character dialog playing and hides its subtitles (immediate: without fading out)",
	    "[immediate:bool | ]", stop_all);
	reg("ark_sound_dialog_play_overlap",
	    "starts a character dialog line while another plays, both heard and both subtitles shown",
	    "name:string", play_overlap);
	log_msg("dialog: script commands ark_sound_dialog_play_interrupt, _stop_all, _play_overlap registered");
}

void dialog_level_begin(void)
{
	g_nextra = 0;
	g_next_text = 0;
	for (u32 k = 0; k < RecentTexts; k++) g_texts[k][0] = 0;
}

void dialog_install(void)
{
	int h = patch_call(OSiDialog_Start_HaltCall, 0x481d90, (u32)halt_dialog);
	int a = patch_call(OSiDialog_Start_CutsceneCall, 0x430480, (u32)cutscene_print);
	int b = patch_call(OSiDialog_Start_InGameCall, 0x4304e0, (u32)ingame_print);
	if (!(h && a && b))
		log_msg("dialog: OSiDialog_Start changed (0x%x %d, 0x%x %d, 0x%x %d): Arklite's dialogue commands won't work fully",
		        OSiDialog_Start_HaltCall, h, OSiDialog_Start_CutsceneCall, a, OSiDialog_Start_InGameCall, b);
}
