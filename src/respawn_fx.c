/* The particle a multiplayer respawn fires, whatever the map, made from the player's own game: no
 * class is in this DLL, only level 0's classes' names and our edits to them (respawn_fx_data.h, made
 * by tools/gen_respawn_fx.py from a Catwalk project's classes), so a map needs nothing and isn't
 * rebuilt.
 *
 * A class's data is used in place and setting the class up writes pointers into it, so a loaded
 * class is no use as a starting point. The class loader in Oni's binary data table (0x53b708) is
 * wrapped instead, and keeps each class it's named here as level 0 loads it. A class that isn't what
 * the edits were made from (size or hash) is left out, and the effect with it: level 0 is the same
 * on every PC of a game (net.c), so all of them go without.
 *
 * Oni's own binary loaders take the results, as if the level held them: the particle table then
 * treats them as the level's classes (particle_table.c), and the group and impulse join level 0's
 * sounds under a name of their own. Particle classes go at level end, so every custom level makes
 * them again. The campaign's levels aren't custom levels, and get nothing. */
#include "arklite.h"

void *memcpy(void *, const void *, unsigned);

enum { FX_PARTICLE, FX_GROUP, FX_IMPULSE };

typedef struct {
	int kind;
	const char *name;
	const char *base;   /* level 0's class it's made from; 0: data is the whole thing */
	u32 base_size, base_hash;
	const u8 *data;     /* base: edits, each { u32 offset, length, n; u8 bytes[n] }: length bytes of
	                     * the class from offset, then the n bytes, padded to 4 */
	u32 size, made_size;
} FxData;

#include "respawn_fx_data.h"

/* BDtMethod_Load: identifier, data; byte swap, locked, allocated */
typedef u32 (ONI_FC *BinaryLoad)(const char *, void *, u32, u32, u32);
static const BinaryLoad g_loaders[] = {
	[FX_PARTICLE] = (BinaryLoad)0x459f20,   /* P3rLoadParticleDefinition */
	[FX_GROUP]    = (BinaryLoad)0x513950,   /* OSiGroup_Load */
	[FX_IMPULSE]  = (BinaryLoad)0x513d90,   /* OSiImpulse_Load */
};

#define BINARY_LOAD_SLOT 0x53b708   /* the particle class loader, in the binary data table */
#define FX_COUNT (sizeof g_fx_data / sizeof g_fx_data[0])

#define FX_BUFFER 0x8000
static u8 g_buffer[FX_BUFFER] __attribute__((aligned(4)));
static u8 g_stock[RESPAWN_FX_STOCK] __attribute__((aligned(4)));   /* level 0's classes, as loaded */
static const u8 *g_stock_at[FX_COUNT];
static u32 g_stock_used;
static BinaryLoad g_class_load;

static u32 fnv1a(const u8 *p, u32 n)
{
	u32 h = 0x811c9dc5;
	while (n--) h = (h ^ *p++) * 0x01000193;
	return h;
}

static u32 ONI_FC class_load(const char *id, void *data, u32 swap, u32 locked, u32 allocated)
{
	for (u32 i = 0; i < FX_COUNT; i++) {
		const FxData *fx = &g_fx_data[i];
		u32 size = ((const u32 *)data)[1] + 8;

		if (!fx->base || g_stock_at[i] || !str_ieq_n(id, fx->base, 64)) continue;
		if (size != fx->base_size || fnv1a(data, size) != fx->base_hash) {
			log_msg("respawn fx: level 0's %s isn't the one the edits are for (%d bytes, hash %08x)", id, size,
			        fnv1a(data, size));
			continue;
		}
		memcpy(g_stock + g_stock_used, data, size);
		g_stock_at[i] = g_stock + g_stock_used;
		g_stock_used += (size + 3) & ~3u;
	}
	return g_class_load(id, data, swap, locked, allocated);
}

/* The class at dest, from level 0's and our edits */
static int fx_make(const FxData *fx, const u8 *stock, u8 *dest)
{
	u32 at = 0;

	for (const u8 *p = fx->data; p < fx->data + fx->size; ) {
		const u32 *op = (const u32 *)p;
		u32 offset = op[0], length = op[1], n = op[2];

		if (offset + length > fx->base_size || at + length + n > fx->made_size) return 0;
		memcpy(dest + at, stock + offset, length);
		at += length;
		memcpy(dest + at, op + 3, n);
		at += n;
		p = (const u8 *)(op + 3) + ((n + 3) & ~3u);
	}
	return at == fx->made_size;
}

const char *respawn_fx_root(void) { return RESPAWN_FX_ROOT; }

void respawn_fx_load(u32 level)
{
	u32 used = 0;

	if (!custom_game_level_ini(level)) return;
	for (u32 i = 0; i < FX_COUNT; i++)
		if (g_fx_data[i].base && !g_stock_at[i]) {
			log_msg("respawn fx: no %s from level 0: no respawn effect", g_fx_data[i].base);
			return;
		}
	for (u32 i = 0; i < FX_COUNT; i++) {
		const FxData *fx = &g_fx_data[i];

		if (used + fx->made_size > FX_BUFFER) {
			log_msg("respawn fx: buffer too small for %s", fx->name);
			return;
		}
		if (!fx->base) {
			memcpy(g_buffer + used, fx->data, fx->size);
		} else if (!fx_make(fx, g_stock_at[i], g_buffer + used)) {
			log_msg("respawn fx: bad edits for %s", fx->name);
			return;
		}
		g_loaders[fx->kind](fx->name, g_buffer + used, 0, 1, 0);
		used = (used + fx->made_size + 3) & ~3u;
	}
}

void respawn_fx_install(void)
{
	u32 loader = (u32)class_load;

	if (*(u32 *)BINARY_LOAD_SLOT != (u32)g_loaders[FX_PARTICLE]) {
		log_msg("respawn fx: unexpected loader at 0x%x, no respawn effect", BINARY_LOAD_SLOT);
		return;
	}
	g_class_load = g_loaders[FX_PARTICLE];
	patch_write(BINARY_LOAD_SLOT, (const u8 *)&loader, 4);
}
