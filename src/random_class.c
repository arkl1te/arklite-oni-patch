/* No dream-lab classes in random costumes.
 *
 * A placed character with RandomCostume (flag 0x2) or UpgradeDifficulty (0x800) doesn't keep its
 * class: ONrGameState_NewCharacter (0x4dac50) walks every loaded ONCC with
 * TMrInstance_GetDataPtr_ByNumber (call at 0x4dae84), keeps the ones whose variant name
 * (ONCV +4, or the Hard name at +0x24) equals the character's, and picks one at random.
 *
 * In retail each level file held its own classes, and striker_dream_lab (a striker_easy at half
 * size) and SH_dream_lab (a Shinatama at 7x) existed only in level 13, where no randomized
 * character uses those variants. AE moved every class into level0_Final.dat, so they joined the
 * striker_easy and Shinatama pools in every level: about one random striker in four came out
 * tiny. We route that one lookup through a function that hands the loop a stand-in class whose
 * variant name matches nothing whenever the real class is a *_dream_lab one, so it is skipped.
 * The dream lab places those classes directly, which doesn't go through this loop. */
#include "arklite.h"

#define ONcNewCharacter_ClassLookupCall 0x4dae84

/* The loop only reads the class's variant pointer (+0x42c) and the variant's name (+4).
 * 0xff can't start a class name, and an empty target name doesn't match it either. */
static u8 g_no_variant[8] = { 0, 0, 0, 0, 0xff, 0 };
static struct { u8 pad[0x42c]; void *variant; } g_unmatched_class = { { 0 }, g_no_variant };

static int is_dream_lab(void *cls)
{
	static const char suffix[] = "_dream_lab";
	const char *name = TMrInstance_GetInstanceName(cls);
	unsigned n, k = sizeof suffix - 1;

	if (!name) return 0;
	n = str_len(name);
	return n > k && str_ieq_n(name + n - k, suffix, k);
}

static u16 ONI_FC class_lookup(u32 tag, u32 index, void **cls)
{
	u16 err = TMrInstance_GetDataPtr_ByNumber(tag, index, cls);

	if (!err && *cls && is_dream_lab(*cls))
		*cls = &g_unmatched_class;
	return err;
}

void random_class_install(void)
{
	if (patch_call(ONcNewCharacter_ClassLookupCall, (u32)TMrInstance_GetDataPtr_ByNumber, (u32)class_lookup))
		log_msg("random costumes: dream lab classes excluded");
}
