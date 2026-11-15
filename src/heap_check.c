/* Heap check: Oni's small-block heap, watched cheaply all the time.
 *
 * Oni's C runtime (VS2003, the single-threaded one: no heap lock) serves blocks up to 1016 bytes
 * from its small-block heap: __sbh_alloc_block 0x524196 (called from malloc 0x51fc70, realloc
 * 0x51fd01, calloc 0x523d21), __sbh_resize_block 0x52464b (realloc 0x51fcf0) and
 * __sbh_free_block 0x523e6b (free 0x51fc0b, realloc 0x51fd21 and 0x51fd67). A block ("entry") is
 * 16-byte aligned, its size rounded up to 16; an allocated one holds size+1 in its first word
 * (the dword before the pointer malloc returns) and in its last; a free one holds its size there
 * and its free-list links at +4 and +8.
 *
 * The joiner of a 2026-11-13 match crashed at the level's unload freeing a static decal's
 * 208-byte block (P3rDeleteDecal 0x453630): the block's first word was 0xd0, so the heap took it
 * for free already, read its "next" block from a misaligned address and wrote through a garbage
 * link (0x523f1d). The log couldn't show who freed it first or when.
 *
 * So every free, resize and allocation passes through here:
 *   - a free is checked first: the block's first word marks it allocated, its size is a heap
 *     size, its last word matches its first (else something wrote past its end), and a free
 *     neighbour's links point into the heap. A bad one isn't freed (a small leak instead of the
 *     crash): logged with the code freeing it, what the journal knows of the block and the game
 *     rewinds since, and sent as a "heap" problem report.
 *   - the journal keeps, by block address, its last allocation and its last free: the tick, what
 *     was running (a tick's first run, a rollback's re-run, a rollback check's run, outside a
 *     tick, another thread), the game code that asked (the first return address on the stack
 *     outside the runtime and UUrMemory) and the number of game rewinds so far. Plus the last
 *     REWINDS rewinds (rollback.c: from which tick back to which). Arklite memory: rollback never
 *     puts it back.
 *   - a call from a thread other than the game's main thread is logged (the first few callers):
 *     with no heap lock, two threads in it at once can break it.
 * The crash log (crash_log.c) asks heap_check_fault for what the journal knows of the blocks
 * the registers point into. Cost: a few dozen instructions a call. */
#include "arklite.h"

void *memset(void *, int, unsigned);

__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) u32    WINAPI GetCurrentThreadId(void);

#define ONgGameState       (*(u8 **)0x5ece7c)
#define GS_TICK(gs)        (*(u32 *)((gs) + 0x144))

#define SBH_ALLOC_FN       0x524196   /* __sbh_alloc_block(size) */
#define SBH_RESIZE_FN      0x52464b   /* __sbh_resize_block(header, block, size) */
#define SBH_FREE_FN        0x523e6b   /* __sbh_free_block(header, block) */
#define SBH_HEADERS        (*(u8 **)0x6d91fc)   /* the regions' headers, 5 dwords each */
#define SBH_NHEADERS       (*(u32 *)0x6d91f8)
#define SBH_REGION_SIZE    0x41c4     /* a region's bookkeeping (32 groups of 64 lists) */
#define SBH_DATA_SIZE      0x100000   /* a region's blocks */
#define SBH_MAX_ENTRY      0x400      /* (1016 + 0x17) & ~0xf */
static const u32 g_alloc_calls[] = { 0x51fc70, 0x51fd01, 0x523d21 };
static const u32 g_free_calls[] = { 0x51fc0b, 0x51fd21, 0x51fd67 };
#define RESIZE_CALL        0x51fcf0

/* the game's code, for "who asked": Oni.exe's text without the runtime and UUrMemory */
#define TEXT_LO            0x401000
#define TEXT_HI            0x528000
#define CRT_LO             0x51e000
#define MEM_LO             0x424a00
#define MEM_HI             0x425c00

enum { K_TICK = 1, K_RERUN, K_CHECK, K_OUTSIDE, K_THREAD };

typedef struct {
	u32 ptr;
	u16 size;
	u8 a_kind, f_kind;            /* 0: not seen */
	u32 a_tick, a_from, a_rewinds, a_seq;
	u32 f_tick, f_from, f_rewinds, f_seq;
} Block;

#define BLOCK_BITS 17
#define REWINDS    1024
typedef struct { u32 serial, from, to; } Rewind;

static Block *g_blocks;
static Rewind g_rewind[REWINDS];
static u32 g_rewinds;            /* game rewinds so far (rollback.c) */
static u32 g_seq;                /* allocations and frees so far: which of a block's came last */
static u32 g_count[2][K_THREAD + 1];   /* this level's allocations [0] and frees [1], by kind */
static u32 g_main_tid, g_on;
static u32 g_bad, g_bad_logs, g_thread_logs, g_thread_from[4];

static const char *kind_name(u32 k)
{
	return k == K_TICK ? "a tick's first run" : k == K_RERUN ? "a rollback's re-run" : k == K_CHECK ? "a rollback check's run" :
	       k == K_OUTSIDE ? "outside a tick" : k == K_THREAD ? "another thread" : "?";
}

static u32 kind_now(void)
{
	int r;
	if (GetCurrentThreadId() != g_main_tid) return K_THREAD;
	if (!lockstep_in_tick()) return K_OUTSIDE;
	r = net_rerun();
	return r == 2 ? K_CHECK : r ? K_RERUN : K_TICK;
}

static u32 tick_now(void)
{
	u8 *gs = ONgGameState;
	return gs ? GS_TICK(gs) : 0;
}

/* A return address: code in Oni.exe right after a call */
static int return_address(u32 a)
{
	const u8 *p = (const u8 *)a;
	if (a < TEXT_LO + 8 || a >= TEXT_HI) return 0;
	return p[-5] == 0xe8 || (p[-2] == 0xff && (p[-1] & 0x38) == 0x10) || (p[-3] == 0xff && (p[-2] & 0x38) == 0x10) ||
	       (p[-6] == 0xff && (p[-5] & 0x38) == 0x10) || (p[-7] == 0xff && (p[-6] & 0x38) == 0x10);
}

static int game_code(u32 a) { return a < CRT_LO && (a < MEM_LO || a >= MEM_HI); }

/* Up to max return addresses into the game's code on the stack from sp up (nearest first) */
static u32 callers(const u32 *sp, u32 *out, u32 max)
{
	u32 top, n = 0;
	__asm__ volatile("movl %%fs:4, %0" : "=r"(top));   /* TEB StackBase */
	for (u32 i = 0; i < 256 && (u32)(sp + i) < top && n < max; i++)
		if (return_address(sp[i]) && game_code(sp[i])) out[n++] = sp[i];
	return n;
}

static u32 caller(void)
{
	u32 c = 0;
	callers(__builtin_frame_address(0), &c, 1);
	return c;
}

static Block *slot_of(u32 ptr)
{
	return &g_blocks[((ptr >> 4) ^ (ptr >> 20) * 0x9e3779b1u) & ((1u << BLOCK_BITS) - 1)];
}

static Block *find(u32 ptr)
{
	Block *b = g_blocks ? slot_of(ptr) : 0;
	return b && b->ptr == ptr ? b : 0;
}

static void note_thread(const char *what, u32 n, u32 from)
{
	if (g_thread_logs >= 4) return;
	for (u32 i = 0; i < g_thread_logs; i++)
		if (g_thread_from[i] == from) return;
	g_thread_from[g_thread_logs++] = from;
	log_msg("heap: thread %u (not the game's main thread) used Oni's small-block heap: %s of %u bytes from 0x%x. Oni's runtime has "
	        "no heap lock, so two threads in it at once can break it", GetCurrentThreadId(), what, n, from);
}

static void note_alloc(u32 p, u32 n)
{
	Block *b;
	u32 k = kind_now(), from = caller();
	if (k == K_THREAD) note_thread("an allocation", n, from);
	g_count[0][k]++;
	if (!p || !g_blocks) return;
	b = slot_of(p);
	if (b->ptr != p) memset(b, 0, sizeof *b);
	b->ptr = p;
	b->size = (u16)n;
	b->a_kind = (u8)k;
	b->a_tick = tick_now();
	b->a_from = from;
	b->a_rewinds = g_rewinds;
	b->a_seq = ++g_seq;
}

void *__cdecl heap_sbh_alloc(u32 n)
{
	void *p = ((void *(__cdecl *)(u32))SBH_ALLOC_FN)(n);
	if (g_on) note_alloc((u32)p, n);
	return p;
}

/* Why the block at entry (its first word) can't be freed, 0 if it can */
static const char *bad_entry(const u8 *header, const u32 *entry)
{
	u32 front = entry[0], size = front - 1, data = *(const u32 *)(header + 0xc), region = *(const u32 *)(header + 0x10);
	if (!(front & 1)) return "the heap says it's free (freed already, or a game rewind put back its free state, or its first word was overwritten)";
	if (size < 0x10 || size > SBH_MAX_ENTRY || (size & 0xf) || ((u32)entry & 0xfff) + size > 0xffc)
		return "its first word isn't a block size (overwritten)";
	if (*(const u32 *)((const u8 *)entry + size - 4) != front)
		return "its last word doesn't match its first (freed already and joined to a free neighbour, or something wrote past its end)";
	{
		const u32 *next = (const u32 *)((const u8 *)entry + size);
		if (!(next[0] & 1)) {   /* a free neighbour: the free joins it and unlinks it (where the 2026-11-13 crash was) */
			for (int i = 1; i <= 2; i++) {
				u32 l = next[i];
				if (!((l >= data && l < data + SBH_DATA_SIZE) || (l >= region && l < region + SBH_REGION_SIZE)))
					return "the free block after it has broken links (overwritten)";
			}
		}
	}
	return 0;
}

static void log_rewinds(const Block *b)
{
	u32 n = 0, last = 0, oldest = g_rewinds > REWINDS ? g_rewinds - REWINDS : 0;
	for (u32 s = b->a_rewinds > oldest ? b->a_rewinds : oldest; s < g_rewinds; s++) {
		const Rewind *r = &g_rewind[s % REWINDS];
		if (r->to <= b->a_tick) { n++; last = s % REWINDS + 1; }
	}
	if (!g_rewinds) return;
	if (!n) {
		log_msg("heap:   no game rewind since its allocation went back to before it");
		return;
	}
	const Rewind *r = &g_rewind[last - 1];
	log_msg("heap:   %u game rewinds since its allocation went back to before it: they made it free in the heap's own "
	        "memory%s; the last, rewind %u, from tick %u back to %u", n,
	        b->a_rewinds < oldest ? " (older rewinds not kept)" : "", r->serial, r->from, r->to);
}

static void bad_free(const u32 *entry, const char *why, const u32 *sp)
{
	u32 c[8], n = callers(sp, c, 8), ptr = (u32)(entry + 1), k = kind_now();
	char list[8 * 12 + 1];
	const Block *b = find(ptr);

	g_bad++;
	if (g_bad_logs >= 8) return;
	g_bad_logs++;
	list[0] = 0;
	for (u32 i = 0; i < n; i++) fmt(list + str_len(list), "%s0x%x", i ? ", " : "", c[i]);
	/* a second free: the first may have joined the block with a free neighbour, so its words say
	 * "free" or no longer match, whichever; the journal tells it apart from an overwrite */
	if (b && b->f_kind && b->f_seq > b->a_seq) why = "it was freed already (a second free)";
	log_msg("heap: tick %u (%s): a block at %08x (first word %08x) can't be freed: %s. Left alone: a small leak instead of a crash",
	        tick_now(), kind_name(k), ptr, entry[0], why);
	log_msg("heap:   freed from Oni.exe %s (nearest first)", n ? list : "?");
	if (!b) log_msg("heap:   the journal has nothing on it (never allocated through the small-block heap, or written over by another block)");
	else {
		if (b->a_kind)
			log_msg("heap:   allocated at tick %u (%s, after %u rewinds), %u bytes, from 0x%x", b->a_tick, kind_name(b->a_kind),
			        b->a_rewinds, b->size, b->a_from);
		if (b->f_kind && b->f_seq > b->a_seq)
			log_msg("heap:   then freed at tick %u (%s, after %u rewinds), from 0x%x", b->f_tick, kind_name(b->f_kind),
			        b->f_rewinds, b->f_from);
		else if (b->a_kind)
			log_msg("heap:   no free of it since that allocation");
		else
			log_msg("heap:   freed at tick %u (%s, after %u rewinds), from 0x%x; its allocation isn't in the journal", b->f_tick,
			        kind_name(b->f_kind), b->f_rewinds, b->f_from);
		log_rewinds(b);
	}
	if (g_bad_logs == 1) report_net(REPORT_HEAP, net_session_id(), net_local_slot(), tick_now(), 0, 0, rollback_check_path());
}

static void __cdecl heap_sbh_free(u8 *header, void *block)
{
	u32 *entry = (u32 *)block - 1;
	if (g_on) {
		const char *why = bad_entry(header, entry);
		if (why) {
			bad_free(entry, why, __builtin_frame_address(0));
			return;
		}
		u32 k = kind_now(), from = caller();
		if (k == K_THREAD) note_thread("a free", entry[0] - 1, from);
		g_count[1][k]++;
		if (g_blocks) {
			Block *b = slot_of((u32)block);
			if (b->ptr != (u32)block) {
				memset(b, 0, sizeof *b);
				b->ptr = (u32)block;
				b->size = (u16)(entry[0] - 1);
			}
			b->f_kind = (u8)k;
			b->f_tick = tick_now();
			b->f_from = from;
			b->f_rewinds = g_rewinds;
			b->f_seq = ++g_seq;
		}
	}
	((void (__cdecl *)(u8 *, void *))SBH_FREE_FN)(header, block);
}

/* rollback.c's in-place resize goes through here: a block that can't be freed isn't resized
 * either (realloc then moves it, and its free here leaves it alone) */
int __cdecl heap_sbh_resize(u8 *header, void *block, u32 n)
{
	if (g_on && bad_entry(header, (u32 *)block - 1)) return 0;
	int ok = ((int (__cdecl *)(u8 *, void *, u32))SBH_RESIZE_FN)(header, block, n);
	if (ok && g_on) note_alloc((u32)block, n);
	return ok;
}

/* rollback.c: the game put back from tick from to the start of tick to */
void heap_check_rewound(u32 from, u32 to)
{
	Rewind *r = &g_rewind[g_rewinds % REWINDS];
	r->serial = g_rewinds + 1;
	r->from = from;
	r->to = to;
	g_rewinds++;
}

/* particle_table.c, at every level's end: this level's counts */
void heap_check_level_end(void)
{
	const u32 *a = g_count[0], *f = g_count[1];
	if (!g_on) return;
	log_msg("heap: this level's small blocks: %u allocated (%u in ticks' first runs, %u in re-runs, %u in rollback checks, %u outside "
	        "ticks, %u by other threads), %u freed (%u, %u, %u, %u, %u); %u bad frees left alone, %u game rewinds so far",
	        a[1] + a[2] + a[3] + a[4] + a[5], a[1], a[2], a[3], a[4], a[5], f[1] + f[2] + f[3] + f[4] + f[5], f[1], f[2], f[3], f[4], f[5],
	        g_bad, g_rewinds);
	memset(g_count, 0, sizeof g_count);
}

/* crash_log.c: what the journal knows of a block a register points into */
void heap_check_fault(const char *reg, u32 v)
{
	if (!g_blocks || v < 0x10000) return;
	for (u32 k = 0; k < 64; k++) {
		u32 p = (v & ~0xfu) - k * 16;
		const Block *b = find(p);
		if (!b) continue;
		if (v - p > (u32)b->size + 8) return;
		log_msg("  %s points into a %u-byte block at %08x (first word %08x): allocated at tick %u (%s) from 0x%x, %u rewinds ago", reg,
		        b->size, p, ((const u32 *)p)[-1], b->a_tick, kind_name(b->a_kind), b->a_from, g_rewinds - b->a_rewinds);
		if (b->f_kind)
			log_msg("  %s's block was %s freed at tick %u (%s) from 0x%x, %u rewinds ago", reg, b->f_seq > b->a_seq ? "then" : "before that",
			        b->f_tick, kind_name(b->f_kind), b->f_from, g_rewinds - b->f_rewinds);
		return;
	}
}

void heap_check_install(void)
{
	u32 alloc_ok = 0, free_ok = 0, resize_ok;
	g_main_tid = GetCurrentThreadId();
	for (u32 i = 0; i < 3; i++) {
		alloc_ok += code_matches(g_alloc_calls[i], (const u8[]){ 0xe8 }, 1) &&
		            g_alloc_calls[i] + 5 + *(const u32 *)(g_alloc_calls[i] + 1) == SBH_ALLOC_FN;
		free_ok += code_matches(g_free_calls[i], (const u8[]){ 0xe8 }, 1) &&
		           g_free_calls[i] + 5 + *(const u32 *)(g_free_calls[i] + 1) == SBH_FREE_FN;
	}
	resize_ok = code_matches(RESIZE_CALL, (const u8[]){ 0xe8 }, 1) && RESIZE_CALL + 5 + *(const u32 *)(RESIZE_CALL + 1) == SBH_RESIZE_FN;
	if (GetEnvironmentVariableA("ARKLITE_HEAP_CHECK", 0, 0) && free_ok) {   /* ARKLITE_HEAP_CHECK=0: off (tests) */
		char v[4];
		GetEnvironmentVariableA("ARKLITE_HEAP_CHECK", v, sizeof v);
		if (v[0] == '0') free_ok = 0;
	}
	if (free_ok != 3 || alloc_ok != 3 || !resize_ok || !(g_blocks = VirtualAlloc(0, sizeof(Block) << BLOCK_BITS, 0x3000, 4))) {
		log_msg("heap: Oni's small-block heap isn't as expected (%u of 3 frees, %u of 3 allocations, %u resize) or turned off: not checked",
		        free_ok, alloc_ok, resize_ok);
		return;
	}
	for (u32 i = 0; i < 3; i++) {
		patch_call(g_alloc_calls[i], SBH_ALLOC_FN, (u32)heap_sbh_alloc);
		patch_call(g_free_calls[i], SBH_FREE_FN, (u32)heap_sbh_free);
	}
	patch_call(RESIZE_CALL, SBH_RESIZE_FN, (u32)heap_sbh_resize);
	g_on = 1;
	log_msg("heap: Oni's small-block heap is checked at every free, with a journal of its blocks (%u KB)",
	        (u32)(sizeof(Block) << BLOCK_BITS) / 1024);
}
