/* Levels can bring their own particle classes.
 *
 * Stock Oni only really has level 0's. Any level's classes do reach P3rLoadParticleDefinition
 * (0x459f20), which puts them on the end of the class array (0x65c520, up to 0x800 of 0x98 bytes,
 * count at 0x6a8666), but two things were never done for them:
 *
 * - P3rLoad_PostProcess (0x4574e0) runs once, after level 0. It's what sets up a class: its action
 *   and emitter pointers, its emitters' classes. So a level's class is left with those NULL, and the
 *   first particle made from it faults at 0x455f85.
 * - The sorted table of names (0x6a8520) has room for level 0's classes alone. A name it hasn't got
 *   is looked for in the rest of the array, from 0x6a8524 (level 0's count) up. But all five lookups
 *   (0x457240, 0x4574e0, 0x457e50, 0x45a030, 0x462d70) run their binary search up to the current
 *   count, so a name that sorts late steps off the table's end and reads what's behind it; a NULL
 *   faults at 0x457340. w9_scr_x01 with 921 + 10 classes: lo 916, hi 930, index 923. Which names go
 *   wrong depends on the number of classes, so one more class can start a crash.
 *
 * Nor are they ever taken out: the array keeps entries whose data went with the level.
 *
 * So:
 * - The table gets its full size, and past level 0's classes it holds an entry named to sort after
 *   every name. A search that gets there turns back, misses, and walks the level's classes as it was
 *   meant to. Only names are read from the table, so a name is all that entry has.
 * - Once a level's data is in (before 0x514970 looks up its sounds and such), its classes go through
 *   the game's own post-process: they take the array's front for the call, as if they were all there
 *   is, and everything goes back after it. Emitters and actions can name level 0's classes, which
 *   that call couldn't see, so those are looked up again.
 * - When the level ends (before its data goes), the count goes back to level 0's. */
#include "arklite.h"

void *memcpy(void *d, const void *s, unsigned n);

#define P3cMaxClasses      0x800
#define P3cClassSize       0x98
#define P3gClasses         ((u8 *)0x65c520)
#define P3gClassTable      (*(const char ***)0x6a8520)
#define P3gClassesInTable  (*(u16 *)0x6a8524)
#define P3gClassCount      (*(u16 *)0x6a8666)

/* In a class: */
#define P3cDefinition      0x48       /* P3tParticleDefinition * */
#define P3cMemoryBlock     0x50       /* the definition's block, if the class owns it */
/* In a definition: */
#define P3cNumEmitters     0x14       /* u16 */
#define P3cEmitters        0x300      /* each P3cEmitterSize, starting with the name of its class */
#define P3cEmitterSize     0x1bc
#define P3cEmittedClass    0x40       /* in an emitter */

#define P3cTableAlloc      0x4575cb   /* the table's size: from the count on the stack */
#define P3cSortCall        0x5059b3   /* the one call of 0x4574e0, after level 0 */
#define P3cLevelCall       0x505de4   /* in level load, the first call after the binary data */
#define P3cLevelEndCall    0x505552
#define P3rLoad_PostProcess ((u32 (ONI_FC *)(u32, u32))0x4574e0)
#define P3rLevelEnd        ((u32 (ONI_FC *)(u32, u32))0x458320)
#define ONrLevelBegin      ((u32 (ONI_FC *)(u32, u32))0x514970)
/* Only OSrLevel_Load's level 0 branch calls these, so an ambient or impulse sound a level defines
 * (OSBD .amb, .imp) keeps NULL groups and plays nothing. Catwalk's single recordings are those. */
#define SSrAmbient_UpdateGroupPointers ((void (*)(void))0x482670)
#define SSrImpulse_UpdateGroupPointers ((void (*)(void))0x480ab0)
#define P3rGetClass        ((u8 *(ONI_FC *)(const char *))0x45a030)
#define P3rSetupActionData ((void (ONI_FC *)(u8 *))0x457200)
#define UUrMemory_Block_Delete ((void (ONI_FC *)(void *))0x424b30)

static const char g_past_every_name[] = "\xff";
static u8 g_level0_classes[P3cMaxClasses * P3cClassSize];

static u32 ONI_FC post_process_hook(u32 ecx, u32 edx)
{
	u32 result = P3rLoad_PostProcess(ecx, edx);

	if (P3gClassTable)
		for (u32 i = P3gClassesInTable; i < P3cMaxClasses; i++)
			P3gClassTable[i] = g_past_every_name;
	return result;
}

static void process_level_classes(void)
{
	u32 level0 = P3gClassesInTable, total = P3gClassCount;
	u32 added = total - level0, kept;
	const char **table = P3gClassTable;

	if (!level0 || total <= level0) return;
	memcpy(g_level0_classes, P3gClasses, level0 * P3cClassSize);
	for (u32 i = 0; i < added * P3cClassSize; i++)   /* to the front: they may overlap */
		P3gClasses[i] = P3gClasses[level0 * P3cClassSize + i];
	P3gClassCount = (u16)added;
	P3rLoad_PostProcess(0, 0);    /* sorts them, and may keep fewer */
	kept = P3gClassCount;
	UUrMemory_Block_Delete(P3gClassTable);
	P3gClassTable = table;
	for (u32 i = kept * P3cClassSize; i-- > 0; )     /* to the end: backwards, the same way */
		P3gClasses[level0 * P3cClassSize + i] = P3gClasses[i];
	memcpy(P3gClasses, g_level0_classes, level0 * P3cClassSize);
	P3gClassesInTable = (u16)level0;
	P3gClassCount = (u16)(level0 + kept);

	for (u32 i = 0; i < kept; i++) {
		u8 *cls = P3gClasses + (level0 + i) * P3cClassSize;
		u8 *def = *(u8 **)(cls + P3cDefinition);
		u8 *emitter = *(u8 **)(def + P3cEmitters);

		for (u32 e = 0; e < *(u16 *)(def + P3cNumEmitters); e++, emitter += P3cEmitterSize)
			*(u8 **)(emitter + P3cEmittedClass) = P3rGetClass((const char *)emitter);
		P3rSetupActionData(cls);
	}
	log_msg("particle table: %d of the level's %d classes set up", kept, added);
}

static u32 ONI_FC level_begin_hook(u32 ecx, u32 edx)
{
	respawn_fx_load(ecx);
	players_level_begin();
	process_level_classes();
	u32 result = ONrLevelBegin(ecx, edx);
	SSrAmbient_UpdateGroupPointers();
	SSrImpulse_UpdateGroupPointers();
	return result;
}

static u32 ONI_FC level_end_hook(u32 ecx, u32 edx)
{
	vertex_light_level_end();
	u32 result = P3rLevelEnd(ecx, edx);
	u32 level0 = P3gClassesInTable;

	if (level0) {
		for (u32 i = level0; i < P3gClassCount; i++) {
			void **block = (void **)(P3gClasses + i * P3cClassSize + P3cMemoryBlock);
			if (*block) UUrMemory_Block_Delete(*block);
			*block = 0;
		}
		P3gClassCount = (u16)level0;
	}
	return result;
}

void particle_table_install(void)
{
	/* mov ecx, [esp+30h]; add esp, 10h; shl ecx, 2 */
	static const u8 expected[10] = { 0x8b, 0x4c, 0x24, 0x30, 0x83, 0xc4, 0x10, 0xc1, 0xe1, 0x02 };
	/* add esp, 10h; mov ecx, P3cMaxClasses * 4; nop; nop */
	static const u8 sized[10] = { 0x83, 0xc4, 0x10, 0xb9, 0x00, 0x20, 0x00, 0x00, 0x90, 0x90 };

	if (!code_matches(P3cTableAlloc, expected, 10) || *(const u8 *)P3cSortCall != 0xe8) {
		log_msg("particle table: unexpected code at 0x%x, left alone", P3cTableAlloc);
		return;
	}
	if (patch_call(P3cSortCall, 0x4574e0, (u32)post_process_hook) &&
	    patch_call(P3cLevelCall, 0x514970, (u32)level_begin_hook) &&
	    patch_call(P3cLevelEndCall, 0x458320, (u32)level_end_hook)) {
		patch_write(P3cTableAlloc, sized, 10);
		log_msg("particle table: levels can add classes");
	}
}
