/* Joining a host whose level0 differs, without restarting Oni.
 *
 * A joiner refused for level0 alone, with the host's pair in AE\cache\<dat hash>-<raw hash>\, reloads
 * the level (net.c) and this runs at that load's TMrLevel_Load call, while no level is loaded:
 * the installed level0 is never written; the swap lasts until Oni closes.
 *
 * The old level 0 can't be freed: the window manager, fonts and everything set up after
 * ONrLevel_LoadZero (0x505690) keep pointers into it. So it stays loaded as a ghost: its instances
 * and its file get a level number of their own, and its instance and name counts go to 0. Old
 * pointers still resolve; lookups by name or number only find the new file. What read the ghost
 * afterwards, measured with it set no-access through level load, play, reload and exit: 29 of ~42000
 * pages, UI parts, fonts, the cursor list and some textures, no animation, character or sound.
 * So menus and fonts look like the old file's until Oni restarts. Details: docs/multiplayer.md.
 *
 * What is built from level 0 and must be rebuilt, because it points into the old file: the sound
 * variant list, impact effects, texture materials and the particle classes. Old classes stay in
 * the ghost (P3rTerminate would also free data LoadZero doesn't make again). */
#include "arklite.h"
#include "oni.h"

#define SSrStopAll                    ((void (ONI_FC *)(void))0x483710)
#define OSrVariantList_LevelUnload    ((void (ONI_FC *)(void))0x515ff0)
#define OSrVariantList_Terminate      ((void (ONI_FC *)(void))0x516060)
#define ONrImpactEffects_Terminate    ((void (ONI_FC *)(void))0x4ffc50)
#define ONrTextureMaterials_Terminate ((void (ONI_FC *)(void))0x517770)
#define ONrLevel_LoadZero             ((u16 (ONI_FC *)(void))0x505690)
#define BDrRegisterClass              0x47ec80
#define BDcRegisterCallTM             0x517514   /* in ONrTextureMaterials_Initialize */
#define BDcRegisterCallIE             0x4ffaf4   /* in ONrImpactEffects_Initialize */
#define P3gClassCount                 (*(u16 *)0x6a8666)
#define P3gClassTable                 (*(u32 *)0x6a8520)
#define P3gClassesInTable             (*(u16 *)0x6a8524)
#define TMgLoadedFiles                ((u8 **)0x570198)
#define TMgLoadedFileCount            (*(u16 *)0x57119c)
#define LEVEL0_INDEX                  1          /* file_index of level0_Final */
#define GHOST_LEVEL_FIRST             126        /* above every real level number */
#define GHOSTS_MAX                    3          /* each costs ~172 MB of Oni's 2 GB */
#define TMcDynamicPixelsMov           0x420f47   /* mov edx, 0x200000; mov ecx, 0x100 (the run-long dynamic file) */
#define DYNAMIC_PIXELS                (16u << 20)

static char g_path[0x100];   /* the level file table's slot 0 entry points here after a swap */
static char g_dir[0x100];
static int g_ghosts, g_loading;

static u16 ONI_FC register_class(u32 tag, void *methods)
{
	return g_loading ? 0 : ((u16 (ONI_FC *)(u32, void *))BDrRegisterClass)(tag, methods);
}

char *level0_swap_ref(char *ref)
{
	if (!g_dir[0]) return ref;
	for (u32 i = 0; i < TMgLevelFileCount; i++) {
		if (TMgLevelFiles[i].ref != ref || (TMgLevelFiles[i].file_index >> 25) != 0) continue;
		fmt(g_path, "%s\\level0_Final.dat", g_dir);
		TMgLevelFiles[i].ref = g_path;
		log_msg("level0 swap: opening %s", g_path);
		return g_path;
	}
	return ref;
}

static int ghost_level0(void)
{
	u32 ghost = ((u32)(GHOST_LEVEL_FIRST - g_ghosts) << 25) | 1;

	for (u32 i = 0; i < TMgLoadedFileCount; i++) {
		u8 *f = TMgLoadedFiles[i];
		u32 *desc, n;

		if (*(u32 *)(f + 0x20) != LEVEL0_INDEX) continue;
		n = TMmFile_InstanceCount(f);
		desc = TMmFile_Descriptors(f);
		for (u32 k = 0; k < n; k++, desc += 5) {
			u32 *id;
			if ((desc[4] & 2) || !desc[1]) continue;
			id = (u32 *)(desc[1] - 4);
			if (*id == LEVEL0_INDEX) *id = ghost;
		}
		*(u32 *)(f + 0x20) = ghost;
		*(u32 *)(f + 0x28) = 0;
		*(u32 *)(f + 0x34) = 0;
		return 1;
	}
	return 0;
}

int level0_swaps(void) { return g_ghosts; }

int level0_swap(const char *dir)
{
	u16 error;

	if (g_ghosts >= GHOSTS_MAX) {
		log_msg("level0 swap: already swapped %d times, restart Oni", g_ghosts);
		return 0;
	}
	SSrStopAll();
	OSrVariantList_LevelUnload();
	OSrVariantList_Terminate();
	ONrImpactEffects_Terminate();
	ONrTextureMaterials_Terminate();
	P3gClassCount = 0;
	P3gClassTable = 0;
	P3gClassesInTable = 0;
	if (!ghost_level0()) {
		log_msg("level0 swap: level 0 isn't loaded");   /* nothing to go on with: Oni can't recover */
		return 0;
	}
	g_ghosts++;
	str_cpy(g_dir, sizeof g_dir, dir);
	respawn_fx_level0_changed();
	char_menu_level0_changed();
	g_loading = 1;
	error = ONrLevel_LoadZero();
	g_loading = 0;
	log_msg("level0 swap: from %s: error %u", dir, error);
	return !error;
}

void level0_swap_install(void)
{
	/* The dynamic instance file that lasts the whole run (0x4209e0 makes it: 256 instances, 2 MB of
	 * pixels, at 0x420f47 mov edx, 0x200000, which Daodan makes 4 MB): every level 0 load puts its text system's font
	 * textures there and nothing frees them, so the second swap ran out ("Unable to allocate the
	 * pixels", Motoko_Texture.c 197, then "Unable to create texture map", BFW_TextSystem.c 690)
	 * and every texture and font drew white. 16 MB holds the original load and GHOSTS_MAX swaps.
	 * Patched before Oni starts (Arklite loads with Daodan, ahead of 0x4209e0). */
	const u8 *c = (const u8 *)TMcDynamicPixelsMov;
	static const u8 ecx_256[] = { 0xb9, 0x00, 0x01, 0x00, 0x00 };   /* mov ecx, 0x100 after it */
	if (c[0] == 0xba && code_matches(TMcDynamicPixelsMov + 5, ecx_256, sizeof ecx_256)) {
		u32 was = *(const u32 *)(c + 1), size = DYNAMIC_PIXELS;   /* (Daodan makes it 4 MB) */
		if (was < size) {
			patch_write(TMcDynamicPixelsMov + 1, &size, 4);
			log_msg("level0 swap: the run's dynamic textures get %u MB of pixels (was %u KB)", size >> 20, was >> 10);
		}
	} else {
		log_msg("level0 swap: 0x%x isn't the dynamic file's size (%02x %02x %02x %02x %02x): a second level0 swap may draw white",
		        TMcDynamicPixelsMov, c[0], c[1], c[2], c[3], c[4]);
	}
	patch_call(BDcRegisterCallTM, BDrRegisterClass, (u32)register_class);
	patch_call(BDcRegisterCallIE, BDrRegisterClass, (u32)register_class);
}
