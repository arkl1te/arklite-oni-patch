/* Rollback, step 4. Rollback runs this PC's input at once, guesses the other player's, and when
 * the real one arrives puts the game back to that tick and runs the ticks again. A snapshot
 * never leaves the PC that took it, so it can be raw memory, pointers and all, as long as it is
 * exactly the memory the game's ticks write: restoring drawing, sound, input or window state
 * would break them, and leaving out some of the game's state would make the second run differ.
 *
 * Step 4a, the memory survey (ARKLITE_MEMSURVEY=<samples>): finds that memory. From 300 ticks
 * into a level, every 120 ticks, it hashes every writable page of the process (4 KB each) four
 * times: twice back to back at the start of a tick (pages that change then are written by other
 * threads: sound, input), at the end of the tick, and at the start of the first tick of a later
 * frame (between them: drawing and the rest of the frame). After the last sample it writes
 * Arklite-memsurvey.txt: each writable region in runs of pages by what changed them, with its
 * owner (module, heap, the main thread's stack, the game state block *0x5ece7c), and the totals.
 * The hashing stalls the game for a moment at each sample. */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

typedef struct {
	void *BaseAddress, *AllocationBase;
	u32 AllocationProtect, RegionSize, State, Protect, Type;
} MEMORY_BASIC_INFORMATION;

__declspec(dllimport) u32    WINAPI VirtualQuery(const void *, MEMORY_BASIC_INFORMATION *, u32);
__declspec(dllimport) u32    WINAPI GetProcessHeaps(u32, HANDLE *);
__declspec(dllimport) u32    WINAPI GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);

#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_TICK(gs)         (*(u32 *)((gs) + 0x144))
#define GS_STATE_SIZE       0x46b6e0          /* with char_limit.c's 255 characters */

#define MEM_COMMIT          0x1000
#define MEM_IMAGE           0x1000000
#define MEM_MAPPED          0x40000
#define PAGE_GUARD          0x100
#define WRITABLE            (0x04 | 0x08 | 0x40 | 0x80)   /* RW, WRITECOPY, EXECUTE_RW, EXECUTE_WC */
#define PAGE_SHIFT          12
#define PAGES               (1u << 20)        /* 4 GB of address space */
#define MAX_REGIONS         16384
#define MAX_HEAPS           64
#define SURVEY_START        300               /* ticks into the level */
#define SURVEY_EVERY        120

enum { STEP_IDLE, STEP_TICK_END, STEP_NEXT_FRAME };

typedef struct { u32 base, size, alloc, type, protect; } Region;

static u32 g_samples_wanted, g_samples_done, g_step, g_next_tick, g_sample_frame;
static u32 g_frames;   /* every frame's update, from lockstep.c */
static u32 *g_h0, *g_h1;                 /* page hashes, by page number */
static u16 *g_n_async, *g_n_tick, *g_n_frame, *g_n_seen;
static Region *g_regions;
static u32 g_nregions;
static u32 g_own_lo[32], g_own_hi[32], g_nown;   /* the survey's own tables */

static void *alloc_own(u32 bytes)
{
	void *p = VirtualAlloc(0, bytes, 0x3000, 4);
	if (p && g_nown < 32) {
		g_own_lo[g_nown] = (u32)p;
		g_own_hi[g_nown++] = (u32)p + bytes;
	}
	return p;
}

static int own(u32 addr)
{
	for (u32 i = 0; i < g_nown; i++)
		if (addr >= g_own_lo[i] && addr < g_own_hi[i]) return 1;
	return 0;
}

static int writable(const MEMORY_BASIC_INFORMATION *m)
{
	return m->State == MEM_COMMIT && (m->Protect & WRITABLE) && !(m->Protect & PAGE_GUARD);
}

static void enumerate(void)
{
	MEMORY_BASIC_INFORMATION m;
	u32 a = 0x10000;

	g_nregions = 0;
	while (a < 0xfffe0000u && VirtualQuery((void *)a, &m, sizeof m) == sizeof m) {
		u32 base = (u32)m.BaseAddress, size = m.RegionSize;
		if (!size || base + size <= a) break;
		if (writable(&m) && !own(base) && g_nregions < MAX_REGIONS) {
			Region *r = &g_regions[g_nregions++];
			r->base = base;
			r->size = size;
			r->alloc = (u32)m.AllocationBase;
			r->type = m.Type;
			r->protect = m.Protect;
		}
		a = base + size;
	}
}

static u32 hash_page(const u32 *p)
{
	u32 h = 0x811c9dc5u;
	for (u32 i = 0; i < 1024; i += 4)
		h = ((h ^ p[i]) * 0x01000193u ^ p[i + 1] ^ (p[i + 2] << 7) ^ (p[i + 3] >> 3)) * 0x01000193u;
	return h;
}

/* Hash every page of the listed regions still writable now into out[], 0 for gone ones */
static void hash_all(u32 *out)
{
	MEMORY_BASIC_INFORMATION m;
	for (u32 i = 0; i < g_nregions; i++) {
		const Region *r = &g_regions[i];
		for (u32 a = r->base; a < r->base + r->size; ) {
			if (VirtualQuery((void *)a, &m, sizeof m) != sizeof m) break;
			u32 end = (u32)m.BaseAddress + m.RegionSize;
			if (end > r->base + r->size) end = r->base + r->size;
			for (; a < end; a += 0x1000)
				out[a >> PAGE_SHIFT] = writable(&m) ? hash_page((const u32 *)a) : 0;
		}
	}
}

static void compare(const u32 *before, const u32 *after, u16 *count)
{
	for (u32 i = 0; i < g_nregions; i++) {
		const Region *r = &g_regions[i];
		for (u32 p = r->base >> PAGE_SHIFT; p < (r->base + r->size) >> PAGE_SHIFT; p++)
			if (before[p] != after[p] && count[p] < 0xffff) count[p]++;
	}
}

/* ---- The report ---------------------------------------------------------------------------- */

static HANDLE g_out = INVALID_HANDLE_VALUE;
static HANDLE g_heaps[MAX_HEAPS];
static u32 g_nheaps;

static void out(const char *f, ...)
{
	char line[1100];
	va_list ap;
	u32 n = 0, written;
	va_start(ap, f);
	n = (u32)wvsprintfA(line, f, ap);
	va_end(ap);
	line[n++] = '\r';
	line[n++] = '\n';
	if (g_out != INVALID_HANDLE_VALUE) WriteFile(g_out, line, n, &written, 0);
}

static const char *owner(const Region *r, char *buf, u32 size)
{
	u32 stack_base, stack_limit, gs = (u32)ONgGameState;
	__asm__ volatile("movl %%fs:4, %0\n\tmovl %%fs:8, %1" : "=r"(stack_base), "=r"(stack_limit));

	if (gs && gs < r->base + r->size && gs + GS_STATE_SIZE > r->base) {
		fmt(buf, "game state block (*0x5ece7c = 0x%08x)", gs);
		return buf;
	}
	if (r->base >= stack_limit && r->base < stack_base) return "main thread's stack";
	for (u32 i = 0; i < g_nheaps; i++)
		if (r->alloc == (u32)g_heaps[i]) {
			fmt(buf, "heap #%d (0x%08x)", i, (u32)g_heaps[i]);
			return buf;
		}
	if (r->type == MEM_IMAGE) {
		char path[MAX_PATH];
		u32 n = GetModuleFileNameA((void *)r->alloc, path, sizeof path), k = n;
		while (k && path[k - 1] != '\\' && path[k - 1] != '/') k--;
		fmt(buf, "image %s", n ? path + k : "?");
		return buf;
	}
	return r->type == MEM_MAPPED ? "mapped" : "private";
}

static char kind_of(u32 p)
{
	if (!g_n_seen[p]) return '-';
	if (g_n_async[p]) return 'A';                 /* changes with no game code running */
	if (g_n_tick[p] && g_n_frame[p]) return 'B';  /* both in ticks and between them */
	if (g_n_tick[p]) return 'T';
	if (g_n_frame[p]) return 'F';
	return '.';
}

static void report(void)
{
	static const char *names[] = { "unchanged", "ticks only", "frames only (drawing etc.)", "ticks and frames", "other threads" };
	u32 totals[5] = { 0 };
	char buf[300];

	g_out = CreateFileA("Arklite-memsurvey.txt", 0x40000000, 1, 0, 2, 0x80, 0);
	if (g_out == INVALID_HANDLE_VALUE) {
		log_msg("memsurvey: can't create Arklite-memsurvey.txt");
		return;
	}
	g_nheaps = GetProcessHeaps(MAX_HEAPS, g_heaps);
	if (g_nheaps > MAX_HEAPS) g_nheaps = MAX_HEAPS;
	out("Arklite memory survey: %d samples, level tick %d. Per page: T = changed in ticks only,", g_samples_done,
	    ONgGameState ? GS_TICK(ONgGameState) : 0);
	out("F = only between ticks (drawing, the rest of the frame), B = both, A = by other threads,");
	out(". = never changed, - = not there when sampled. Counts are out of the samples.");
	out("Heaps: %d; the game state block is *0x5ece7c.", g_nheaps);
	out("");
	enumerate();
	for (u32 i = 0; i < g_nregions; i++) {
		const Region *r = &g_regions[i];
		u32 p = r->base >> PAGE_SHIFT, end = (r->base + r->size) >> PAGE_SHIFT;
		out("region 0x%08x-0x%08x %6d KB, %s, protect %02x", r->base, r->base + r->size, r->size >> 10,
		    owner(r, buf, sizeof buf), r->protect);
		while (p < end) {
			char k = kind_of(p);
			u32 q = p, tick = 0, frame = 0;
			while (q < end && kind_of(q) == k) {
				if (g_n_tick[q] > tick) tick = g_n_tick[q];
				if (g_n_frame[q] > frame) frame = g_n_frame[q];
				q++;
			}
			if (k != '.' && k != '-')
				out("  %c 0x%08x-0x%08x %6d KB (in ticks up to %d, between up to %d)", k, p << PAGE_SHIFT,
				    q << PAGE_SHIFT, (q - p) * 4, tick, frame);
			u32 t = k == 'T' ? 1 : k == 'F' ? 2 : k == 'B' ? 3 : k == 'A' ? 4 : 0;
			totals[t] += (q - p) * 4;
			p = q;
		}
	}
	out("");
	for (u32 t = 0; t < 5; t++) out("total %s: %d KB", names[t], totals[t]);
	CloseHandle(g_out);
	g_out = INVALID_HANDLE_VALUE;
	log_msg("memsurvey: wrote Arklite-memsurvey.txt: ticks only %d KB, both %d KB, frames only %d KB, other threads %d KB",
	        totals[1], totals[3], totals[2], totals[4]);
}

/* ---- Hooks (lockstep.c) ------------------------------------------------------------------- */

void rollback_frame(void) { g_frames++; }

void rollback_tick_begin(void)
{
	u8 *gs = ONgGameState;
	if (!g_samples_wanted || g_samples_done >= g_samples_wanted || !gs) return;
	u32 tick = GS_TICK(gs);

	if (g_step == STEP_NEXT_FRAME) {
		if (g_frames == g_sample_frame) return;   /* still the same frame */
		hash_all(g_h0);
		compare(g_h1, g_h0, g_n_frame);
		g_samples_done++;
		g_step = STEP_IDLE;
		log_msg("memsurvey: sample %d of %d at tick %d", g_samples_done, g_samples_wanted, tick);
		if (g_samples_done == g_samples_wanted) report();
		return;
	}
	if (!g_next_tick) g_next_tick = tick + SURVEY_START;
	if (tick < g_next_tick) return;
	g_next_tick = tick + SURVEY_EVERY;
	enumerate();
	for (u32 i = 0; i < g_nregions; i++)
		for (u32 p = g_regions[i].base >> PAGE_SHIFT; p < (g_regions[i].base + g_regions[i].size) >> PAGE_SHIFT; p++)
			if (g_n_seen[p] < 0xffff) g_n_seen[p]++;
	hash_all(g_h0);
	hash_all(g_h1);
	compare(g_h0, g_h1, g_n_async);
	hash_all(g_h0);
	g_step = STEP_TICK_END;
}

void rollback_tick_end(void)
{
	if (g_step != STEP_TICK_END) return;
	hash_all(g_h1);
	compare(g_h0, g_h1, g_n_tick);
	g_sample_frame = g_frames;
	g_step = STEP_NEXT_FRAME;
}

/* ---- Step 4b: the snapshot engine ------------------------------------------------------- *
 * Keeps the game's memory at the start of each of the last RB_SLOTS ticks, so the game can be
 * put back to any of them. Which memory: the pages the game's ticks write, found as they are
 * written. While a tick runs, every candidate page not yet known is write-protected; the first
 * write to one faults into rb_fault, which (on the main thread) adds the page to the set and
 * copies its content from before the write into the snapshots, then lets the write go on.
 * Pages written by other threads (sound) become foreign: never protected or restored again,
 * and so do pages two hashes back to back at the engine's start found changing. Candidates
 * (rb_candidate) are only memory known to be Oni's: the writable part of Oni.exe's image,
 * Arklite's .tstate section (TICK_STATE in arklite.h; the rest of Arklite, the network and the
 * log included, must never be rewound), and allocations of Oni's C runtime (VS2003): its heap *0x6d9200 (every
 * allocation HeapWalk shows, large blocks included), its small-block regions (headers at
 * *0x6d91fc, *0x6d91f8 of them, 5 dwords each, +0xc the 1 MB region it reserved, 0x52449f) and
 * the game state block's allocation. Never anything else: protecting another thread's stack
 * kills the process outright (the fault can't be delivered on a stack it can't write). The main
 * stack, mapped files and the engine's own tables are left out too. Protection is only on
 * during a tick, so drawing, input and the window never fault.
 *
 * Sound: Oni's sound thread reads what the ticks write about the sounds playing (channels,
 * positions, buffer pointers), and putting those back under it crashed its mixer. So every
 * RB_WATCH_EVERY ticks (every tick for the first RB_WATCH_FIRST), between ticks, the known
 * pages are made PAGE_NOACCESS until the next tick: the main thread (drawing) touching one only
 * opens it again, another thread touching one makes it shared, and shared pages are never
 * copied, put back or compared again.
 *
 * Locks: a critical section that another thread holds must never be put back (a restore that
 * brought back the sound thread's ownership of one left the game waiting forever). Restores and
 * comparisons skip every live critical section in a page, found by its DebugInfo (+0), whose
 * CriticalSection field (+4) points back at it (rb_lock_mask).
 *
 * Step 4c, the rewind test (ARKLITE_ROLLBACK_TEST=<samples>): from tick RB_START + 60 of a
 * level, every 120 ticks, before the tick runs the game is put back RB_BACK ticks, those ticks
 * run again with their recorded input records (the update loop's tick count increment
 * included; the camera and drawing between them are not), and every known page is compared
 * with the snapshot of this tick from the first time. Then this tick's snapshot goes back in
 * either way and the tick runs as usual. Arklite-rollbacktest.txt gets each sample: the known
 * pages, the foreign ones, and every page that came out different with its owner, the Oni.exe
 * globals that point near it and the first differing dwords. The re-run ticks' sounds play
 * again. Player 2's gamepad input isn't recorded per tick: leave it alone. */

#define ONgCrtHeap       (*(HANDLE *)0x6d9200)
#define RB_SLOTS         12               /* snapshots: ticks t-11..t */
#define RB_BACK          8                /* the test rewinds this far */
#define RB_MAX_PAGES     2048             /* pages a snapshot holds (8 MB) */
#define RB_MAX_RUNS      32768
#define RB_MAX_ONI       8192             /* allocation bases of Oni's memory */
#define ONgSbhHeaders    (*(u32 **)0x6d91fc)
#define ONgSbhCount      (*(u32 *)0x6d91f8)
#define RB_START         240              /* ticks into the level before the engine starts */
#define PF_CAND          1                /* may be in a snapshot */
#define PF_FOREIGN       2                /* written by another thread: never */
#define PF_PROT          4                /* write-protected now */
#define PF_EXEC          8                /* executable (restore EXECUTE_READWRITE) */
#define PF_WATCH         16               /* PAGE_NOACCESS between ticks, to catch other threads */
#define PF_SHARED        32               /* known, but another thread uses it: left alone */
#define RB_WATCH_EVERY   10
#define RB_WATCH_FIRST   120

typedef struct {
	void *lpData;
	u32 cbData;
	u8 cbOverhead, iRegionIndex;
	u16 wFlags;
	u32 more[4];
} PROCESS_HEAP_ENTRY;

typedef struct { u32 code, flags; void *record, *address; u32 nparams, info[15]; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *record; void *context; } EXCEPTION_POINTERS;

__declspec(dllimport) int WINAPI HeapWalk(HANDLE, PROCESS_HEAP_ENTRY *);
__declspec(dllimport) u32 WINAPI GetCurrentThreadId(void);

typedef struct { u32 page, n, old; } Run;

/* All of the engine's changing state lives here, in its own memory */
typedef struct {
	u32 active, main_tid, in_body, start_tick, last_tick, start_now;
	u8 *flag;                      /* by page: PF_* */
	u16 *index;                    /* by page: 1 + its place in pages[], 0 not known */
	u32 pages[RB_MAX_PAGES], npages, overflow, nforeign_pages;
	u8 *slot[RB_SLOTS];            /* RB_MAX_PAGES pages each, in pages[] order */
	u32 slot_tick[RB_SLOTS], slot_valid[RB_SLOTS];
	u8 action[RB_SLOTS][0x30];
	u8 has_action[RB_SLOTS];       /* 0: the tick ran with no record (more ticks than records) */
	u32 cur;                       /* the slot of the tick running now */
	Run runs[RB_MAX_RUNS];
	u32 nruns;
	Region *regs;
	u32 nregs;
	u32 oni[RB_MAX_ONI], noni;     /* allocation bases of Oni's runtime memory */
	u32 samples_wanted, samples_done, next_sample, total_bad, ticks_protected, last_runs, nshared;
	u32 fault_page, fault_tid, fault_repeat, forced;   /* a fault that keeps coming back */
	u8 mask[1024];                 /* rb_lock_mask: dwords of live critical sections */
	u8 *gimg, *gnow;               /* every character's state before the rewind, and one now */
	u8 ginuse[256];
	u32 gseed[2], total_game_bad;
	HANDLE out;                    /* the report (here, not in Arklite's own data) */
	u8 tmp_action[0x30];
	char buf[300];
} Engine;

static Engine *g_e;

static void rb_unwatch(void);
static int rb_kept(u32 p);

/* Still committed and writable (a tick may have freed it) */
static int page_writable(u32 p)
{
	MEMORY_BASIC_INFORMATION m;
	return VirtualQuery((const void *)(p << PAGE_SHIFT), &m, sizeof m) == sizeof m && writable(&m);
}

static void rb_add_oni(const void *a)
{
	MEMORY_BASIC_INFORMATION m;
	u32 base, k = 0;
	if (!a || VirtualQuery(a, &m, sizeof m) != sizeof m || m.State != MEM_COMMIT) return;
	base = (u32)m.AllocationBase;
	while (k < g_e->noni && g_e->oni[k] != base) k++;
	if (k == g_e->noni && g_e->noni < RB_MAX_ONI) g_e->oni[g_e->noni++] = base;
}

/* The allocations Oni's C runtime made */
static void rb_find_oni(void)
{
	HANDLE crt = ONgCrtHeap;
	PROCESS_HEAP_ENTRY e;
	MEMORY_BASIC_INFORMATION m;
	u32 lo = 0, hi = 0;

	g_e->noni = 0;
	if (crt) {
		e.lpData = 0;
		while (HeapWalk(crt, &e)) {
			u32 a = (u32)e.lpData;
			if (a >= lo && a < hi) continue;   /* most entries share the last one's region */
			if (VirtualQuery(e.lpData, &m, sizeof m) != sizeof m) continue;
			lo = (u32)m.BaseAddress;
			hi = lo + m.RegionSize;
			rb_add_oni(e.lpData);
		}
	}
	for (u32 i = 0; ONgSbhHeaders && i < ONgSbhCount; i++) rb_add_oni((const void *)ONgSbhHeaders[i * 5 + 3]);
	rb_add_oni(ONgGameState);
}

static int rb_image_ok(u32 alloc)
{
	return alloc == 0x400000;   /* Oni.exe; Arklite only through its .tstate section (rb_start) */
}

extern char __ImageBase;

/* Arklite's .tstate section: start and size, 0 if missing */
static u32 rb_tstate(u32 *size)
{
	u8 *base = (u8 *)&__ImageBase;
	u32 pe = *(u32 *)(base + 0x3c);
	u16 nsec = *(u16 *)(base + pe + 6), opt = *(u16 *)(base + pe + 20);
	u8 *sec = base + pe + 24 + opt;
	for (u32 i = 0; i < nsec; i++, sec += 40)
		if (str_ieq_n((const char *)sec, ".tstate", 8)) {
			*size = (*(u32 *)(sec + 8) + 0xfff) & ~0xfffu;
			return (u32)base + *(u32 *)(sec + 12);
		}
	*size = 0;
	return 0;
}

static int rb_candidate(const Region *r)
{
	u32 stack_base, stack_limit;
	__asm__ volatile("movl %%fs:4, %0\n\tmovl %%fs:8, %1" : "=r"(stack_base), "=r"(stack_limit));

	if (r->base >= 0x7ff00000u || r->type == MEM_MAPPED) return 0;
	if (r->base < stack_base && r->base + r->size > stack_limit) return 0;
	if (r->type == MEM_IMAGE) return rb_image_ok(r->alloc);
	for (u32 k = 0; k < g_e->noni; k++)
		if (g_e->oni[k] == r->alloc) return 1;
	return 0;
}

static u8 *slot_page(u32 slot, u32 i) { return g_e->slot[slot] + i * 0x1000; }

static int memcmp_pages(const u8 *a, const u8 *b)
{
	const u32 *x = (const u32 *)a, *y = (const u32 *)b;
	for (u32 i = 0; i < 1024; i++)
		if (x[i] != y[i]) return 1;
	return 0;
}

/* The first write to a protected page */
static int rb_open_now(u32 p)
{
	MEMORY_BASIC_INFORMATION m;
	return VirtualQuery((const void *)(p << PAGE_SHIFT), &m, sizeof m) == sizeof m && (m.Protect & WRITABLE);
}

static long WINAPI rb_fault(EXCEPTION_POINTERS *ep)
{
	EXCEPTION_RECORD *r = ep->record;
	Engine *e = g_e;
	u32 p, old, tid;

	if (!e || r->code != 0xc0000005u || r->nparams < 2) return 0;
	p = r->info[1] >> PAGE_SHIFT;
	if (p >= PAGES || !(e->flag[p] & PF_CAND)) return 0;
	tid = GetCurrentThreadId();
	if (p == e->fault_page && tid == e->fault_tid) {
		if (++e->fault_repeat > 4) {   /* never loop: open it for good */
			e->flag[p] |= PF_FOREIGN | PF_SHARED;
			VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
			e->flag[p] &= ~(PF_WATCH | PF_PROT);
			e->forced++;
			e->fault_repeat = 0;
			return -1;
		}
	} else {
		e->fault_page = p;
		e->fault_tid = tid;
		e->fault_repeat = 0;
	}
	if (e->flag[p] & PF_WATCH) {   /* a known page between ticks: who touches it? */
		if (tid != e->main_tid && !(e->flag[p] & PF_SHARED)) {
			e->flag[p] |= PF_SHARED;
			e->nshared++;
		}
		VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
		e->flag[p] &= ~PF_WATCH;
		return -1;
	}
	if (!(e->flag[p] & PF_PROT)) return rb_open_now(p) ? -1 : 0;   /* another thread just opened it */
	if (r->info[0] != 1) return 0;
	if (tid == e->main_tid && e->in_body && e->npages < RB_MAX_PAGES) {
		u32 i = e->npages++;
		e->pages[i] = p;
		e->index[p] = (u16)(i + 1);
		for (u32 s = 0; s < RB_SLOTS; s++)   /* not written in a tick since any of them */
			if (e->slot_valid[s]) memcpy(slot_page(s, i), (const void *)(p << PAGE_SHIFT), 0x1000);
	} else {
		if (tid == e->main_tid) e->overflow++;
		else e->nforeign_pages++;
		e->flag[p] |= PF_FOREIGN;
	}
	VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
	e->flag[p] &= ~PF_PROT;
	return -1;   /* EXCEPTION_CONTINUE_EXECUTION: the access runs again, now allowed */
}

static void rb_start(void)
{
	Engine *e = g_e;

	rb_find_oni();
	enumerate();
	e->nregs = 0;
	memset(e->flag, 0, PAGES);
	memset(e->index, 0, PAGES * 2);
	for (u32 i = 0; i < g_nregions; i++) {
		const Region *r = &g_regions[i];
		if (!rb_candidate(r) || e->nregs >= MAX_REGIONS) continue;
		e->regs[e->nregs++] = *r;
		memset(e->flag + (r->base >> PAGE_SHIFT), PF_CAND | (r->protect & 0xf0 ? PF_EXEC : 0), r->size >> PAGE_SHIFT);
	}
	{
		u32 size, start = rb_tstate(&size);
		if (start && e->nregs < MAX_REGIONS) {
			Region *r = &e->regs[e->nregs++];
			r->base = start;
			r->size = size;
			r->alloc = (u32)&__ImageBase;
			r->type = MEM_IMAGE;
			r->protect = 0x04;
			memset(e->flag + (start >> PAGE_SHIFT), PF_CAND, size >> PAGE_SHIFT);
		} else {
			log_msg("rollback: Arklite's .tstate section is missing: its game state won't be rewound");
		}
	}
	/* other threads: two hashes back to back */
	hash_all(g_h0);
	hash_all(g_h1);
	for (u32 i = 0; i < g_nregions; i++)
		for (u32 p = g_regions[i].base >> PAGE_SHIFT; p < (g_regions[i].base + g_regions[i].size) >> PAGE_SHIFT; p++)
			if (g_h0[p] != g_h1[p] && (e->flag[p] & PF_CAND)) {
				e->flag[p] |= PF_FOREIGN;
				e->nforeign_pages++;
			}
	e->npages = e->overflow = 0;
	for (u32 s = 0; s < RB_SLOTS; s++) e->slot_valid[s] = 0;
	e->main_tid = GetCurrentThreadId();
	e->active = 1;
	u32 pages = 0;
	for (u32 i = 0; i < e->nregs; i++) pages += e->regs[i].size >> PAGE_SHIFT;
	log_msg("rollback: snapshots on: %d allocations of Oni's runtime, %d candidate regions (%d KB), %d pages changed by "
	        "other threads at the start", e->noni, e->nregs, pages * 4, e->nforeign_pages);
}

static void rb_reset(const char *why)
{
	if (!g_e->active) return;
	rb_unwatch();
	log_msg("rollback: snapshots off (%s); %d pages were known", why, g_e->npages);
	g_e->active = 0;
}

static void rb_snapshot(u32 tick, const void *action)
{
	Engine *e = g_e;
	u32 s = tick % RB_SLOTS;

	for (u32 i = 0; i < e->npages; i++)
		if (rb_kept(e->pages[i])) memcpy(slot_page(s, i), (const void *)(e->pages[i] << PAGE_SHIFT), 0x1000);
	e->slot_tick[s] = tick;
	e->slot_valid[s] = 1;
	e->has_action[s] = action != 0;
	if (action) memcpy(e->action[s], action, 0x30);
	e->cur = s;
}

/* Put the game back to the start of a kept tick */
/* In one of the writable regions listed at the engine's start (g_regions, in address order) */
static int rb_readable(u32 a)
{
	u32 lo = 0, hi = g_nregions;
	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (a < g_regions[mid].base) hi = mid;
		else if (a >= g_regions[mid].base + g_regions[mid].size) lo = mid + 1;
		else return 1;
	}
	return 0;
}

/* g_e->mask[k] = 1 for the dwords of the page that belong to a live critical section: its
 * DebugInfo (+0) points to a debug record whose CriticalSection field (+4) points back */
static void rb_lock_mask(u32 p)
{
	const u32 *page = (const u32 *)(p << PAGE_SHIFT);
	memset(g_e->mask, 0, sizeof g_e->mask);
	for (u32 k = 0; k < 1024; k++) {
		u32 d = page[k];
		if ((d & 3) || d < 0x10000 || !rb_readable(d) || !rb_readable(d + 7)) continue;
		if (*(const u32 *)(d + 4) != (u32)(page + k)) continue;
		for (u32 n = k; n < k + 6 && n < 1024; n++) g_e->mask[n] = 1;   /* 24 bytes */
	}
}

static void rb_restore_page(u32 p, const u8 *from)
{
	u32 *page = (u32 *)(p << PAGE_SHIFT);
	const u32 *src = (const u32 *)from;
	rb_lock_mask(p);
	for (u32 k = 0; k < 1024; k++)
		if (!g_e->mask[k]) page[k] = src[k];
}

static void rb_restore(u32 slot)
{
	Engine *e = g_e;
	for (u32 i = 0; i < e->npages; i++)
		if (rb_kept(e->pages[i])) rb_restore_page(e->pages[i], slot_page(slot, i));
}

/* The page differs from a copy, live critical sections left out */
static int rb_page_differs(u32 p, const u8 *want)
{
	const u32 *now = (const u32 *)(p << PAGE_SHIFT), *w = (const u32 *)want;
	if (!memcmp_pages((const u8 *)now, want)) return 0;
	rb_lock_mask(p);
	for (u32 k = 0; k < 1024; k++)
		if (!g_e->mask[k] && now[k] != w[k]) return 1;
	return 0;
}

static void rb_protect(void)
{
	Engine *e = g_e;
	e->nruns = 0;
	for (u32 i = 0; i < e->nregs; i++) {
		const Region *r = &e->regs[i];
		u32 p = r->base >> PAGE_SHIFT, end = (r->base + r->size) >> PAGE_SHIFT;
		while (p < end) {
			while (p < end && (e->index[p] || (e->flag[p] & PF_FOREIGN))) p++;
			u32 q = p;
			while (q < end && !e->index[q] && !(e->flag[q] & PF_FOREIGN)) q++;
			if (q > p && e->nruns < RB_MAX_RUNS) {
				Run *run = &e->runs[e->nruns];
				for (u32 k = p; k < q; k++) e->flag[k] |= PF_PROT;
				if (VirtualProtect((void *)(p << PAGE_SHIFT), (q - p) << PAGE_SHIFT, e->flag[p] & PF_EXEC ? 0x20 : 0x02, &run->old)) {
					run->page = p;
					run->n = q - p;
					e->nruns++;
				} else {
					for (u32 k = p; k < q; k++) e->flag[k] &= ~PF_PROT;
				}
			}
			p = q;
		}
	}
}

static void rb_unprotect(void)
{
	Engine *e = g_e;
	for (u32 i = 0; i < e->nruns; i++) {
		Run *run = &e->runs[i];
		u32 old;
		VirtualProtect((void *)(run->page << PAGE_SHIFT), run->n << PAGE_SHIFT, run->old, &old);
		for (u32 k = run->page; k < run->page + run->n; k++) e->flag[k] &= ~PF_PROT;
	}
	e->last_runs = e->nruns;
	e->nruns = 0;
}

static void rb_watch(void)
{
	Engine *e = g_e;
	u32 old;
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		if (e->flag[p] & (PF_SHARED | PF_WATCH)) continue;
		e->flag[p] |= PF_WATCH;
		if (!VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, 0x01 /* PAGE_NOACCESS */, &old)) e->flag[p] &= ~PF_WATCH;
	}
}

static void rb_unwatch(void)
{
	Engine *e = g_e;
	u32 old;
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		if (!(e->flag[p] & PF_WATCH)) continue;
		VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
		e->flag[p] &= ~PF_WATCH;
	}
}

/* A known page that is copied, put back and compared */
static int rb_kept(u32 p) { return !(g_e->flag[p] & PF_SHARED) && page_writable(p); }

/* ---- The game state the rewind must reproduce ----------------------------------------------- *
 * Every character and its active data as the network check sees them (net_char_image: without
 * what drawing writes), and both RNGs. Also left out, as they don't decide play: +0x15f4 (the
 * speech line's sound handle), the upper 3 bytes of +0x1604 and +0x161c (stack leftovers in the
 * speech records) and +0x1640 (ticks since the character was last drawn: drawing resets it, so
 * it differs by PC; it decides when an AI goes inactive, which matters once multiplayer maps
 * have AIs). */
#define RB_MAX_CHARS 255

static void rb_game_capture(void)
{
	for (u32 i = 0; i < RB_MAX_CHARS; i++)
		g_e->ginuse[i] = (u8)net_char_image(i, g_e->gimg + i * NET_CHAR_IMAGE_SIZE);
	lockstep_get_seeds(&g_e->gseed[0], &g_e->gseed[1]);
}

static u32 rb_mask_of(u32 off)
{
	if (off == 0x15f4 || off == 0x1640) return 0;
	if (off == 0x1604 || off == 0x161c) return 0xff;
	return 0xffffffffu;
}

/* Compares with rb_game_capture's; reports and returns the number of differences */
static u32 rb_game_compare(void)
{
	u32 bad = 0, seeds[2];

	lockstep_get_seeds(&seeds[0], &seeds[1]);
	if (seeds[0] != g_e->gseed[0] || seeds[1] != g_e->gseed[1]) {
		out("    GAME RNG differs: %08x/%08x the first time, %08x/%08x now", g_e->gseed[0], g_e->gseed[1], seeds[0], seeds[1]);
		bad++;
	}
	for (u32 i = 0; i < RB_MAX_CHARS; i++) {
		int inuse = net_char_image(i, g_e->gnow);
		if (inuse != g_e->ginuse[i]) {
			out("    CHARACTER #%d %s now", i, inuse ? "exists" : "is gone");
			bad++;
			continue;
		}
		if (!inuse) continue;
		const u32 *a = (const u32 *)(g_e->gimg + i * NET_CHAR_IMAGE_SIZE), *b = (const u32 *)g_e->gnow;
		u32 shown = 0, n = 0;
		for (u32 k = 0; k < NET_CHAR_IMAGE_SIZE / 4; k++) {
			u32 off = k * 4, m = rb_mask_of(off);
			if (((a[k] ^ b[k]) & m) == 0) continue;
			n++;
			if (shown++ < 8)
				out("    CHARACTER #%d %s+0x%04x: %08x the first time, %08x now", i, off < 0x16a0 ? "chr" : "act",
				    off < 0x16a0 ? off : off - 0x16a0, a[k], b[k]);
		}
		if (n) {
			out("    CHARACTER #%d: %d values differ", i, n);
			bad += n;
		}
	}
	return bad;
}

/* ---- The rewind test's report --------------------------------------------------------- */

/* Up to 3 Oni.exe globals that point into or just below the page */
static void rb_globals_near(u32 p, char *out_buf)
{
	u8 *base = (u8 *)0x400000;
	u32 a = p << PAGE_SHIFT, n = 0;
	u32 pe = *(u32 *)(base + 0x3c);
	u16 nsec = *(u16 *)(base + pe + 6), opt = *(u16 *)(base + pe + 20);
	u8 *sec = base + pe + 24 + opt;

	out_buf[0] = 0;
	for (u32 i = 0; i < nsec && n < 3; i++, sec += 40) {
		u32 va = *(u32 *)(sec + 12), vs = *(u32 *)(sec + 8), ch = *(u32 *)(sec + 36);
		if (!(ch & 0x80000000u)) continue;   /* writable sections only */
		for (u32 o = 0; o + 4 <= vs && n < 3; o += 4) {
			u32 v = *(u32 *)(base + va + o);
			if (v >= a - 0x10000 && v < a + 0x1000) {
				u32 len = 0;
				while (out_buf[len]) len++;
				fmt(out_buf + len, " [0x%08x]=0x%08x", 0x400000 + va + o, v);
				n++;
			}
		}
	}
}

static void rb_out_diff(u32 p, const u8 *want)
{
	const u8 *now = (const u8 *)(p << PAGE_SHIFT);
	const char *who = "?";
	char near[160];
	u32 a = p << PAGE_SHIFT, off = 0;

	for (u32 i = 0; i < g_e->nregs; i++)
		if (a >= g_e->regs[i].base && a < g_e->regs[i].base + g_e->regs[i].size) {
			who = owner(&g_e->regs[i], g_e->buf, sizeof g_e->buf);
			break;
		}
	rb_globals_near(p, near);
	rb_lock_mask(p);
	while (off < 0x1000 && (g_e->mask[off / 4] || *(const u32 *)(want + off) == *(const u32 *)(now + off))) off += 4;
	u32 lo = off >= 8 ? off - 8 : 0;
	out("    DIFFERS 0x%08x %s, first at +0x%03x (0x%08x); Oni.exe globals near:%s", a, who, off, a + off,
	    near[0] ? near : " none");
	out("      first time %08x %08x %08x %08x %08x", *(const u32 *)(want + lo), *(const u32 *)(want + lo + 4),
	    *(const u32 *)(want + lo + 8), *(const u32 *)(want + lo + 12), *(const u32 *)(want + lo + 16));
	out("      re-run     %08x %08x %08x %08x %08x", *(const u32 *)(now + lo), *(const u32 *)(now + lo + 4),
	    *(const u32 *)(now + lo + 8), *(const u32 *)(now + lo + 12), *(const u32 *)(now + lo + 16));
}

static void rb_rewind_test(u32 tick, void (*body)(void *))
{
	Engine *e = g_e;
	u8 *gs = ONgGameState;
	u32 now_slot = tick % RB_SLOTS, back = tick - RB_BACK, bad = 0, shown = 0;

	for (u32 t = back; t <= tick; t++)
		if (!e->slot_valid[t % RB_SLOTS] || e->slot_tick[t % RB_SLOTS] != t) return;   /* not kept yet */
	if (e->out == INVALID_HANDLE_VALUE || !e->out) {
		e->out = g_out = CreateFileA("Arklite-rollbacktest.txt", 0x40000000, 1, 0, 2, 0x80, 0);
		out("Arklite rollback test: each sample puts the game back %d ticks, runs them again with their", RB_BACK);
		out("inputs and compares every known page with the first time. Oni's C runtime heap: 0x%08x.", (u32)ONgCrtHeap);
	}

	rb_game_capture();
	rb_restore(back % RB_SLOTS);
	for (u32 t = back; t < tick; t++) {
		memcpy(e->tmp_action, e->action[t % RB_SLOTS], 0x30);
		body(e->has_action[t % RB_SLOTS] ? e->tmp_action : 0);
		GS_TICK(gs)++;   /* as the update loop does after each tick (0x4fb159) */
	}

	g_out = e->out;
	g_nheaps = GetProcessHeaps(MAX_HEAPS, g_heaps);
	if (g_nheaps > MAX_HEAPS) g_nheaps = MAX_HEAPS;
	out("");
	out("sample %d, tick %d: %d known pages (%d shared with other threads, left alone), %d foreign, %d over the limit, "
	    "%d forced open; re-ran ticks %d-%d", e->samples_done + 1, tick, e->npages, e->nshared, e->nforeign_pages,
	    e->overflow, e->forced, back, tick - 1);
	u32 game_bad = rb_game_compare();
	out("  %s", game_bad ? "GAME STATE DIFFERS (above)" : "GAME STATE MATCHED: every character and both RNGs as the first time");
	out("  memory pages that differ (diagnosis; between-tick state such as drawing's is expected here):");
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		if (!rb_kept(p)) continue;
		if (!rb_page_differs(p, slot_page(now_slot, i))) continue;
		bad++;
		if (shown++ < 24) rb_out_diff(p, slot_page(now_slot, i));
	}
	if (!bad) out("    none: every known page came out as the first time");
	rb_restore(now_slot);   /* this tick's start as it was, either way */

	e->samples_done++;
	e->total_bad += bad;
	e->total_game_bad += game_bad;
	log_msg("rollbacktest: sample %d of %d at tick %d: rewound %d ticks; game state %s; %d of %d pages differ",
	        e->samples_done, e->samples_wanted, tick, RB_BACK, game_bad ? "DIFFERS" : "MATCHED", bad, e->npages);
	if (e->samples_done == e->samples_wanted) {
		out("");
		out("total: %d samples; game state differences %d; differing pages %d", e->samples_done, e->total_game_bad,
		    e->total_bad);
		CloseHandle(e->out);
		e->out = g_out = INVALID_HANDLE_VALUE;
		log_msg("rollbacktest: done, %d samples; game state differences: %d%s (Arklite-rollbacktest.txt)",
		        e->samples_done, e->total_game_bad, e->total_game_bad ? "" : " (ALL MATCHED)");
	}
}

/* lockstep.c's tick: the engine around body(action), and the test when it's due */
int rollback_test_tick(void *action, void (*body)(void *))
{
	Engine *e = g_e;
	u8 *gs = ONgGameState;
	u32 tick;

	if (!e || !gs) return 0;
	if (!e->samples_wanted && !e->start_now && !e->active) return 0;   /* neither the test nor a match */
	tick = GS_TICK(gs);
	if (e->active) rb_unwatch();
	if (tick < e->last_tick) {   /* a new level, or the level restarted */
		rb_reset("the level restarted or changed");
		e->start_tick = 0;
	}
	e->last_tick = tick;
	if (!e->active) {
		if (!e->start_tick) e->start_tick = e->start_now ? tick : tick + RB_START;
		if (tick < e->start_tick) return 0;
		rb_start();
		e->next_sample = tick + 60;
	}
	rb_snapshot(tick, action);
	if (e->samples_done < e->samples_wanted && tick >= e->next_sample) {
		e->next_sample = tick + SURVEY_EVERY;
		rb_rewind_test(tick, body);
	}
	if (e->ticks_protected < 3) log_msg("rollback: tick %d: protecting %d known pages' neighbours", tick, e->npages);
	rb_protect();
	e->in_body = 1;
	body(action);
	e->in_body = 0;
	rb_unprotect();
	e->ticks_protected++;
	if (e->ticks_protected <= 3 || !(e->ticks_protected % 600))
		log_msg("rollback: tick %d done (%d protected): %d known pages, %d shared, %d foreign, %d over the limit, %d runs",
		        tick, e->ticks_protected, e->npages, e->nshared, e->nforeign_pages, e->overflow, e->last_runs);
	if (e->ticks_protected <= RB_WATCH_FIRST || !(e->ticks_protected % RB_WATCH_EVERY)) rb_watch();
	return 1;
}

static int rb_alloc(void)
{
	if (g_e) return 1;
	g_e = alloc_own(sizeof *g_e);
	if (!g_e) return 0;
	g_e->flag = alloc_own(PAGES);
	g_e->index = alloc_own(PAGES * 2);
	g_e->regs = alloc_own(MAX_REGIONS * sizeof(Region));
	g_e->gimg = alloc_own(RB_MAX_CHARS * NET_CHAR_IMAGE_SIZE);
	g_e->gnow = alloc_own(NET_CHAR_IMAGE_SIZE);
	if (!g_e->flag || !g_e->index || !g_e->regs || !g_e->gimg || !g_e->gnow) return 0;
	for (u32 s = 0; s < RB_SLOTS; s++)
		if (!(g_e->slot[s] = alloc_own(RB_MAX_PAGES * 0x1000))) return 0;
	return AddVectoredExceptionHandler(1, (void *)rb_fault) != 0;
}

static int alloc_tables(void)
{
	if (g_h0) return 1;
	g_h0 = alloc_own(PAGES * 4);
	g_h1 = alloc_own(PAGES * 4);
	g_n_async = alloc_own(PAGES * 2);
	g_n_tick = alloc_own(PAGES * 2);
	g_n_frame = alloc_own(PAGES * 2);
	g_n_seen = alloc_own(PAGES * 2);
	g_regions = alloc_own(MAX_REGIONS * sizeof(Region));
	return g_h0 && g_h1 && g_n_async && g_n_tick && g_n_frame && g_n_seen && g_regions;
}

/* ---- Step 4d: net.c's rollback ------------------------------------------------------------ */

/* Snapshots from the next tick on, for a networked game that predicts the peer's input */
int rollback_net_begin(void)
{
	if (!alloc_tables() || !rb_alloc()) {
		log_msg("rollback: out of memory, the game can't roll back");
		g_e = 0;
		return 0;
	}
	if (g_e->active) rb_reset("a new match");
	g_e->samples_wanted = 0;
	g_e->start_now = 1;
	g_e->start_tick = 0;
	g_e->last_tick = 0;
	return 1;
}

void rollback_net_end(void)
{
	if (g_e && g_e->active) rb_reset("the match ended");
	if (g_e) g_e->start_now = 0;
}

/* Put the game back to the start of tick, if it's still kept: 1 done, 0 not kept */
int rollback_restore_tick(u32 tick)
{
	Engine *e = g_e;
	u32 s = tick % RB_SLOTS;
	if (!e || !e->active || !e->slot_valid[s] || e->slot_tick[s] != tick) return 0;
	rb_unwatch();
	rb_restore(s);
	e->last_tick = tick;   /* not "the level restarted" */
	return 1;
}

u32 rollback_known_pages(void) { return g_e && g_e->active ? g_e->npages : 0; }

void rollback_install(void)
{
	char v[16];
	const char *s = v;
	float n = 0;

	if (GetEnvironmentVariableA("ARKLITE_ROLLBACK_TEST", v, sizeof v)) {
		if (!parse_float(&s, &n) || n < 1) n = 10;
		if (!alloc_tables() || !rb_alloc()) {
			log_msg("rollbacktest: out of memory, no test");
			g_e = 0;
			return;
		}
		g_e->samples_wanted = (u32)n;
		log_msg("rollbacktest: snapshots from tick %d of the next level; %d samples rewinding %d ticks, every %d ticks; "
		        "writes Arklite-rollbacktest.txt", RB_START, g_e->samples_wanted, RB_BACK, SURVEY_EVERY);
		return;
	}
	if (!GetEnvironmentVariableA("ARKLITE_MEMSURVEY", v, sizeof v)) return;
	if (!parse_float(&s, &n) || n < 1) n = 10;
	if (!alloc_tables()) {
		log_msg("memsurvey: out of memory, no survey");
		return;
	}
	g_samples_wanted = (u32)n;
	log_msg("memsurvey: %d samples from tick %d of the next level, every %d ticks; writes Arklite-memsurvey.txt",
	        g_samples_wanted, SURVEY_START, SURVEY_EVERY);
}
