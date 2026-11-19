/* Weapon classes without a model. A v0.5.0 report (2026-11-15) crashed at Oni.exe 0x41f8b3 on the
 * first frame of every map with a Super Ball Gun lying on the ground, on one PC only: Oni draws an
 * unheld weapon's glow from its class's model (0x519710 calls 0x41f8b0 with ONWC +0x58), and that
 * PC's w5_sbg had none. The crash can't tell whether the class came from a file that way (a mod's
 * ONWC) or lost its model after the level loaded, so this logs it, nothing more:
 *   - at each level's load: every ONWC whose model is 0, with the instance file it came from, and
 *     how many weapon classes each file gives;
 *   - every frame, before the draw: a weapon lying on the ground whose class has no model now,
 *     once a class a level (the line comes before the crash's).
 * An instance's file is at its data -4 once Oni resolved it (TMrInstance_GetInstanceName does);
 * the file's name is its first 32 bytes (0x423d90 compares the file's index at +0x20). */
#include "arklite.h"

#define TAG_ONWC          0x4f4e5743u   /* 'ONWC' */
#define ONWC_MODEL(c)     (*(void **)((u8 *)(c) + 0x58))
#define WPgWeapons        ((u8 *)0x6221c0)   /* 64 weapons, 0x170 bytes each (the glow loop's bounds) */
#define WPcWeaponSize     0x170
#define WPcWeapons        64
#define WP_IN_USE(w)      ((w)[2] & 1)
#define WP_CLASS(w)       (*(u8 **)((w) + 4))
#define WP_OWNER(w)       (*(void **)((w) + 8))
#define ONgGameState      (*(u8 **)0x5ece7c)
#define GS_TICK(gs)       (*(u32 *)((gs) + 0x144))
#define MAX_FILES         8
#define MAX_SAID          16

static u8 *g_said[MAX_SAID];   /* classes named by the frame check this level */
static u32 g_nsaid;

/* the instance file's name, from a data pointer Oni has resolved */
static void file_name(void *data, char *out, u32 size)
{
	const char *f = *(const char **)((u8 *)data - 4);
	u32 n = 0;
	if (!f || ((u32)f & 1)) { str_cpy(out, size, "?"); return; }
	while (n + 1 < size && n < 31 && f[n]) { out[n] = f[n]; n++; }
	out[n] = 0;
}

void weapon_check_level(void)
{
	u32 count = TMrInstance_GetTagCount(TAG_ONWC), bad = 0, nfiles = 0;
	const char *files[MAX_FILES];
	u32 per_file[MAX_FILES];
	char line[400] = "";

	g_nsaid = 0;
	for (u32 i = 0; i < count; i++) {
		void *c = 0;
		const char *name;
		char file[32];
		u32 f;
		if (TMrInstance_GetDataPtr_ByNumber(TAG_ONWC, i, &c) || !c) continue;
		name = TMrInstance_GetInstanceName(c);   /* (resolves the file pointer at data -4) */
		for (f = 0; f < nfiles; f++) if (files[f] == *(const char **)((u8 *)c - 4)) break;
		if (f == nfiles && nfiles < MAX_FILES) { files[nfiles] = *(const char **)((u8 *)c - 4); per_file[nfiles++] = 0; }
		if (f < nfiles) per_file[f]++;
		if (ONWC_MODEL(c)) continue;
		bad++;
		file_name(c, file, sizeof file);
		log_msg("weapons: ONWC %s (from %s) has no model: Oni crashes (0x41f8b3) drawing one that lies on the ground",
		        name ? name : "?", file);
	}
	for (u32 f = 0; f < nfiles; f++) {
		char one[64], file[32];
		const char *p = files[f];
		u32 n = 0;
		if (!p || ((u32)p & 1)) str_cpy(file, sizeof file, "?");
		else { while (n < 31 && p[n]) { file[n] = p[n]; n++; } file[n] = 0; }
		fmt(one, "%s%s %u", f ? ", " : "", file, per_file[f]);
		if (str_len(line) + str_len(one) < sizeof line) str_cpy(line + str_len(line), sizeof line - str_len(line), one);
	}
	log_msg("weapons: %u weapon classes, %s (by file: %s)", count, bad ? "some without a model (above)" : "all with a model",
	        line[0] ? line : "-");
}

void weapon_check_frame(void)
{
	u8 *gs = ONgGameState;
	if (!gs) return;
	for (int i = 0; i < WPcWeapons; i++) {
		u8 *w = WPgWeapons + i * WPcWeaponSize, *c;
		u32 k;
		if (!WP_IN_USE(w) || WP_OWNER(w) || !(c = WP_CLASS(w)) || ONWC_MODEL(c)) continue;
		for (k = 0; k < g_nsaid && g_said[k] != c; k++) {}
		if (k < g_nsaid) continue;
		if (g_nsaid < MAX_SAID) g_said[g_nsaid++] = c;
		{
			const char *name = TMrInstance_GetInstanceName(c);
			char file[32];
			file_name(c, file, sizeof file);
			log_msg("weapons: weapon %d on the ground at tick %u: its ONWC %s (from %s) has no model now; Oni's glow draw "
			        "crashes on it (0x41f8b3)", i, GS_TICK(gs), name ? name : "?", file);
		}
	}
}
