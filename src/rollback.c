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
#define GS_TIME_BASE(gs)    (*(u32 *)((gs) + 0x140))   /* wall clock (1/60 s) the server time was last moved to */
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

	char path[MAX_PATH];
	g_out = log_file_create("Arklite-memsurvey.txt", path);
	if (g_out == INVALID_HANDLE_VALUE) {
		log_msg("memsurvey: can't create %s", path);
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
 * opens it again, another thread touching one makes it shared, and a shared page is put back
 * only in the dwords ticks were seen changing (tmask: its slot against the tick's end) and
 * never seen changing between ticks (omask: the tick's end against the next tick's start).
 * SS2's own state (rb_add_excl) is never put back.
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
/* SS2, Oni's sound system: its globals, the ambient play table (*0x5eb728, *0x5eb720 entries of
 * 0x58 bytes: +0 play id, +4 state (0 free, 7 stopping), +0xc/+0x10 its channels) and the
 * next play id. The sound thread mixes from these, so they are never put back. */
#define SSgGlobalsLo         0x5eb700
#define SSgGlobalsHi         0x5eb760
#define SSgAmbientNextId     (*(volatile u32 *)0x5eb70c)
#define SSgAmbientCount      (*(u32 *)0x5eb720)
#define SSgAmbients          (*(u32 **)0x5eb728)
#define SSrAmbient_Stop      ((void (ONI_FC *)(u32))0x4823c0)   /* ECX play id */
#define SSgChannels1         (*(u8 **)0x5eb74c)   /* sound channels (made by 0x4830c0), *0x63f390 of them */
#define SSgChannels2         (*(u8 **)0x5eb750)   /* and *0x63f388 more */
#define SSgChannelCount1     (*(u32 *)0x63f390)
#define SSgChannelCount2     (*(u32 *)0x63f388)
#define SS_CHANNEL_SIZE      0x10c
#define SSgLock              (*(u8 **)0x63f394)   /* SS2's lock: a CRITICAL_SECTION in Oni's heap (0x487040) */
#define SSgPlaying           (*(u32 **)0x5eb7ec)  /* the channels the sound thread mixes: Oni's array {size, count, grow, capacity, items} */
#define SSrLock              ((void (ONI_FC *)(void *))0x487070)
#define SSrUnlock            ((void (ONI_FC *)(void *))0x487060)
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
#define PF_SHARED        32               /* known, but another thread uses it: put back only where ticks alone write (tmask, omask) */
#define RB_MAX_EXCL      1024             /* ranges never put back (SS2's sound state, open ACM streams) */
#define RB_MAX_NOTES     48               /* pages that became shared, logged from the main thread */
#define RB_WATCH_EVERY   10
#define RB_MAX_DROPPED   1024             /* pages dropped as another heap's, kept for rollback checks */
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
__declspec(dllimport) u32 WINAPI GetTickCount(void);
__declspec(dllimport) void WINAPI Sleep(u32);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) HANDLE WINAPI OpenThread(u32, int, u32);
__declspec(dllimport) u32 WINAPI SuspendThread(HANDLE);
__declspec(dllimport) u32 WINAPI ResumeThread(HANDLE);
__declspec(dllimport) int WINAPI GetThreadContext(HANDLE, void *);

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
	u32 oni_span[RB_MAX_ONI];      /* and each one's size, reserved part included (rb_span) */
	u32 samples_wanted, samples_done, next_sample, total_bad, ticks_protected, last_runs, nshared;
	u32 fault_page, fault_tid, fault_repeat, forced;   /* a fault that keeps coming back */
	volatile u32 body_serial, in_fault;   /* rb_watchdog: ticks begun; the main thread is in rb_fault */
	volatile long learn_lock;      /* rb_learn from the main thread, another thread and the watchdog */
	u8 *omask;                     /* by place in pages[]: 128 bytes, the dwords seen changing between ticks */
	u8 *tmask;                     /* by place in pages[]: 128 bytes, the dwords seen changing in a tick */
	u8 *endc;                      /* by place in pages[]: shared pages as the last tick ended */
	u8 endc_have[RB_MAX_PAGES];    /* endc holds this page (it was shared when the last tick ended) */
	u32 endc_ok;                   /* endc holds the last tick's end (no restore since) */
	u32 amb_lo[RB_SLOTS], amb_hi[RB_SLOTS];   /* SS2 ambient play ids the slot's tick started: (lo, hi] */
	u32 ex_lo[RB_MAX_EXCL], ex_hi[RB_MAX_EXCL], nex;
	u32 note_page[RB_MAX_NOTES], note_tid[RB_MAX_NOTES], nnotes, notes_logged;
	u32 amb_stopped;
	u32 acm_hits, acm_logs;        /* rb_acm_guard: ACM memory found in known pages */
	u32 grow, grown, grow_logs;    /* rb_grow: Oni took memory the snapshots don't cover; pages added; logged */
	u32 freed, freed_logs;         /* rb_forget: pages of allocations Oni gave back */
	u32 reused, heap_pages, heap_logs, heap_walked;   /* rb_check_allocs, rb_heaps_guard */
	u32 seg_kept;                  /* rb_restore_page: Wine heap segment links a restore would have rewound */
	u32 dropped[RB_MAX_DROPPED], ndropped;            /* rb_heaps_guard's pages, for rollback checks */
	u8 *vpages, *vdropped;         /* rollback checks: known pages and dropped pages after the first re-run */
	u32 vn, vnd, vchecks, vbad, vlogs;
	HANDLE vout;
	u32 plist_logs, plist_cuts;    /* rb_particles_check */
	u32 stack_top, rescues, rescue_learned;
	HANDLE main_thread;
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
static void rb_unprotect(void);
static int rb_kept(u32 p);
static void rb_globals_near(u32 p, char *out_buf);
static void rb_sound_before_restore(u32 tick);

/* Still committed and writable (a tick may have freed it) */
static int page_writable(u32 p)
{
	MEMORY_BASIC_INFORMATION m;
	return VirtualQuery((const void *)(p << PAGE_SHIFT), &m, sizeof m) == sizeof m && writable(&m);
}

/* The size of the allocation at base, its reserved pages included: it never changes while the
 * allocation lives, so a different one means it was freed and the address handed out again */
static u32 rb_span(u32 base)
{
	MEMORY_BASIC_INFORMATION m;
	u32 a = base;
	while (a < 0x7ff00000u && VirtualQuery((const void *)a, &m, sizeof m) == sizeof m && (u32)m.AllocationBase == base &&
	       m.State != 0x10000 /* MEM_FREE */ && m.RegionSize)
		a = (u32)m.BaseAddress + m.RegionSize;
	return a - base;
}

static void rb_add_oni(const void *a)
{
	MEMORY_BASIC_INFORMATION m;
	u32 base, k = 0;
	if (!a || VirtualQuery(a, &m, sizeof m) != sizeof m || m.State != MEM_COMMIT) return;
	base = (u32)m.AllocationBase;
	while (k < g_e->noni && g_e->oni[k] != base) k++;
	if (k == g_e->noni && g_e->noni < RB_MAX_ONI) {
		g_e->oni_span[g_e->noni] = rb_span(base);
		g_e->oni[g_e->noni++] = base;
	}
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

/* A protected page the main thread is about to write in this tick: known from now on, its
 * content (as at the tick's start: nothing wrote it since) copied into the kept snapshots */
static void rb_learn(u32 p)
{
	Engine *e = g_e;
	u32 i = e->npages++;
	e->endc_have[i] = 0;
	memset(e->omask + i * 128, 0, 128);
	memset(e->tmask + i * 128, 0, 128);
	for (u32 s = 0; s < RB_SLOTS; s++)   /* not written in a tick since any of them */
		if (e->slot_valid[s]) memcpy(slot_page(s, i), (const void *)(p << PAGE_SHIFT), 0x1000);
	e->pages[i] = p;
	e->index[p] = (u16)(i + 1);
}

static void rb_lock(void)
{
	while (__sync_lock_test_and_set(&g_e->learn_lock, 1))
		;
}

static void rb_unlock(void) { __sync_lock_release(&g_e->learn_lock); }

/* A page another thread uses: noted for the log (the main thread writes it at the tick's end) */
static void rb_note(u32 p, u32 tid)
{
	Engine *e = g_e;
	if (e->nnotes < RB_MAX_NOTES) {
		e->note_page[e->nnotes] = p;
		e->note_tid[e->nnotes] = tid;
		e->nnotes++;
	}
}

static long rb_fault_main(EXCEPTION_POINTERS *ep, u32 tid);

static long WINAPI rb_fault(EXCEPTION_POINTERS *ep)
{
	Engine *e = g_e;
	u32 tid = GetCurrentThreadId();
	long r;
	/* the match's snapshots are off (rb_reset opened every page): any fault now is someone
	 * else's. Handled, a real crash during the next level's load looped here forever (the Windows
	 * joiner froze going back to the lobby after a match) */
	if (!e || !e->active) return 0;
	if (tid != e->main_tid) return rb_fault_main(ep, tid);
	e->in_fault = 1;
	r = rb_fault_main(ep, tid);
	e->in_fault = 0;
	return r;
}

static long rb_fault_main(EXCEPTION_POINTERS *ep, u32 tid)
{
	EXCEPTION_RECORD *r = ep->record;
	Engine *e = g_e;
	u32 p, old;

	if (r->code != 0xc0000005u || r->nparams < 2) return 0;
	p = r->info[1] >> PAGE_SHIFT;
	if (p >= PAGES || !(e->flag[p] & PF_CAND)) return 0;
	if (p == e->fault_page && tid == e->fault_tid) {
		if (e->fault_repeat > 4 + 4) return 0;   /* opening it didn't help: not ours, let it crash */
		if (++e->fault_repeat > 4) {   /* never loop: open it for good */
			e->flag[p] |= PF_FOREIGN | PF_SHARED;
			VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
			e->flag[p] &= ~(PF_WATCH | PF_PROT);
			e->forced++;
			e->fault_repeat = 4 + 4 + 1;   /* the next fault here in a row goes on to the other handlers */
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
			rb_note(p, tid);
		}
		VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
		e->flag[p] &= ~PF_WATCH;
		return -1;
	}
	if (!(e->flag[p] & PF_PROT)) return rb_open_now(p) ? -1 : 0;   /* another thread just opened it */
	if (r->info[0] != 1) return 0;
	if (tid == e->main_tid && e->in_body && e->npages < RB_MAX_PAGES) {
		rb_lock();
		rb_learn(p);
		rb_unlock();
	} else if (tid != e->main_tid && e->npages < RB_MAX_PAGES) {
		/* another thread writes it in a tick: known and shared, so the dwords only ticks write are
		 * still put back once it has been watched (pages left out made particles come apart) */
		rb_lock();
		if (!e->index[p] && e->npages < RB_MAX_PAGES) {
			rb_learn(p);
			e->flag[p] |= PF_SHARED;
			e->nshared++;
			rb_note(p, tid);
		}
		rb_unlock();
	} else {
		if (tid == e->main_tid) e->overflow++;
		else e->nforeign_pages++;
		e->flag[p] |= PF_FOREIGN;
	}
	VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
	e->flag[p] &= ~PF_PROT;
	return -1;   /* EXCEPTION_CONTINUE_EXECUTION: the access runs again, now allowed */
}

#define POOL_SLOTS        96            /* 6 MB (see "Oni's big blocks during a match") */
static u8 *g_pool;
static u32 g_pool_used_max;          /* the most pool slots in use at once this match */

static void rb_start(void)
{
	Engine *e = g_e;

	rb_find_oni();
	if (g_pool) rb_add_oni(g_pool);   /* the pool for Oni's big blocks is game memory too (see "Oni's big blocks") */
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
	/* other threads: two hashes back to back. A page that changed between them is another
	 * thread's too (the sound thread mixing: the match's start sound plays right now), but ticks
	 * may write game state into it as well (Oni's heap mixes them). Once marked foreign, never
	 * copied or put back: the Linux host of 2026-11-06 started a match with 26 such pages and
	 * came apart at tick 7140 (the host's own player 0.386 units off on the joiner); its other
	 * sessions started with 0 or 1 and stayed in sync. So they're known and shared from the
	 * start, as a page another thread touches later is: put back only in the dwords ticks
	 * alone change (tmask, omask). Known pages are never protected, so no new faults. */
	hash_all(g_h0);
	hash_all(g_h1);
	e->npages = e->overflow = 0;
	{
		u32 n = 0, logged = 0;
		for (u32 i = 0; i < g_nregions; i++)
			for (u32 p = g_regions[i].base >> PAGE_SHIFT; p < (g_regions[i].base + g_regions[i].size) >> PAGE_SHIFT; p++) {
				if (g_h0[p] == g_h1[p] || !(e->flag[p] & PF_CAND)) continue;
				if (e->npages >= RB_MAX_PAGES) {
					e->flag[p] |= PF_FOREIGN;
					e->nforeign_pages++;
					continue;
				}
				rb_learn(p);
				e->flag[p] |= PF_SHARED;
				e->nshared++;
				n++;
				if (logged++ < 8) {
					char near[160];
					rb_globals_near(p, near);
					log_msg("rollback: page %08x changed under another thread at the start: shared from the start; Oni.exe "
					        "globals near:%s", p << PAGE_SHIFT, near[0] ? near : " none");
				}
			}
		if (n) log_msg("rollback: %d page(s) changed by other threads at the start are known and shared (put back where ticks "
		               "alone write)", n);
	}
	for (u32 s = 0; s < RB_SLOTS; s++) e->slot_valid[s] = 0;
	e->endc_ok = 0;
	e->nnotes = e->notes_logged = 0;
	e->amb_stopped = e->acm_hits = e->acm_logs = 0;
	e->grow = e->grown = e->grow_logs = e->plist_logs = e->plist_cuts = e->freed = e->freed_logs = 0;
	e->reused = e->heap_pages = e->heap_logs = e->heap_walked = e->ndropped = e->seg_kept = 0;
	e->vchecks = e->vbad = e->vlogs = 0;
	e->main_tid = GetCurrentThreadId();
	if (!e->main_thread) e->main_thread = OpenThread(0x1fffff /* THREAD_ALL_ACCESS */, 0, e->main_tid);
	__asm__ volatile("movl %%fs:4, %0" : "=r"(e->stack_top));
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
	if (g_e->nruns) rb_unprotect();
	log_msg("rollback: snapshots off (%s); %d pages were known, %d KB taken during the match followed, %d KB given back "
	        "dropped, %d broken particle lists cut, ACM memory in known pages %d times, %d freed addresses taken again, %d KB of "
	        "other heaps dropped", why, g_e->npages, g_e->grown * 4, g_e->freed * 4, g_e->plist_cuts, g_e->acm_hits, g_e->reused,
	        g_e->heap_pages * 4);
	if (g_pool) log_msg("rollback: the pool for Oni's big blocks: at most %d of %d slots in use at once this match", g_pool_used_max, POOL_SLOTS);
	g_pool_used_max = 0;
	g_e->active = 0;
	if (g_e->vout && g_e->vout != INVALID_HANDLE_VALUE) CloseHandle(g_e->vout);
	g_e->vout = 0;
}

static void rb_snapshot(u32 tick, const void *action)
{
	Engine *e = g_e;
	u32 s = tick % RB_SLOTS;

	/* shared pages: the dwords that changed since the last tick ended weren't the ticks'. Only
	 * pages endc holds: one that became shared between ticks (the sound thread touching it while
	 * watched) has no copy from then, and comparing with whatever was there marked most of it as
	 * not the ticks', so a rollback never put it back (the taunt-spam desync) */
	for (u32 i = 0; e->endc_ok && i < e->npages; i++) {
		u32 p = e->pages[i];
		if (!(e->flag[p] & PF_SHARED) || !e->endc_have[i] || !page_writable(p)) continue;
		const u32 *now = (const u32 *)(p << PAGE_SHIFT), *was = (const u32 *)(e->endc + i * 0x1000);
		u8 *m = e->omask + i * 128;
		for (u32 k = 0; k < 1024; k++)
			if (now[k] != was[k]) m[k >> 3] |= (u8)(1 << (k & 7));
	}
	for (u32 i = 0; i < e->npages; i++)
		if ((e->flag[e->pages[i]] & PF_CAND) && page_writable(e->pages[i]))
			memcpy(slot_page(s, i), (const void *)(e->pages[i] << PAGE_SHIFT), 0x1000);
	e->slot_tick[s] = tick;
	e->slot_valid[s] = 1;
	e->has_action[s] = action != 0;
	if (action) memcpy(e->action[s], action, 0x30);
	e->cur = s;
}

/* Readable now. Asked of the system, not of the region list made at the engine's start: memory
 * freed since then (a heap shrinking) made a restore read a decommitted page and crash. The last
 * few readable regions are cached for one restore or comparison (rb_rd_forget clears them:
 * nothing is freed while the main thread is in there). */
#define RB_RD_CACHE 16
static u32 g_rd_lo[RB_RD_CACHE], g_rd_hi[RB_RD_CACHE], g_rd_next;

static int rb_readable(u32 a)
{
	MEMORY_BASIC_INFORMATION m;
	for (u32 i = 0; i < RB_RD_CACHE; i++)
		if (a >= g_rd_lo[i] && a < g_rd_hi[i]) return 1;
	if (VirtualQuery((const void *)a, &m, sizeof m) != sizeof m || m.State != MEM_COMMIT) return 0;
	if ((m.Protect & (0x01 | PAGE_GUARD)) || !(m.Protect & 0xee)) return 0;   /* no access, guard; any read right */
	g_rd_lo[g_rd_next] = (u32)m.BaseAddress;
	g_rd_hi[g_rd_next] = (u32)m.BaseAddress + m.RegionSize;
	g_rd_next = (g_rd_next + 1) % RB_RD_CACHE;
	return 1;
}

static void rb_rd_forget(void)
{
	memset(g_rd_hi, 0, sizeof g_rd_hi);
}

/* Memory that drawing owns inside the game's pages, never put back: Arklite's HUD textures
 * (char_menu.c). A TXMP keeps its size, its pixels pointer and its GL texture name (+0x9c) in
 * the struct, in Oni's dynamic instance file; a rewind that put back the page under one gave it
 * the old values, or another texture's. Here, not in the engine: they're made before it is. */
static u32 g_keep_lo[256], g_keep_hi[256], g_nkeep;

void rollback_keep(const void *p, u32 n)
{
	if (g_nkeep < 256 && p && n) {
		g_keep_lo[g_nkeep] = (u32)p;
		g_keep_hi[g_nkeep++] = (u32)p + n;
	}
}

/* Under Wine a segment of a heap starts on a 64 KB boundary with 0x20 bytes of its own bookkeeping:
 * +0x0c {next, prev} in the heap's segment list, +0x14 the heap. Oni's runtime heap is game memory,
 * so such a page gets rewound, but the list isn't the game's: when a tick emptied a segment, Wine
 * unmapped it and wrote the neighbour's next, which made the neighbour's page known with the old
 * link in its snapshot. Put back, that link pointed at unmapped memory, and the next walk of the
 * heap (rb_find_oni, at the next match's first tick) faulted in ntdll. Windows keeps other words
 * there, so this finds nothing on Windows. */
static int rb_wine_segment(u32 a, const u32 *page)
{
	return !(a & 0xffff) && page[5] == (u32)ONgCrtHeap;
}

static void rb_keep_mask(u32 p)
{
	u32 a = p << PAGE_SHIFT;
	for (u32 x = 0; x < g_nkeep; x++) {
		if (g_keep_hi[x] <= a || g_keep_lo[x] >= a + 0x1000) continue;
		u32 lo = g_keep_lo[x] > a ? (g_keep_lo[x] - a) / 4 : 0, hi = g_keep_hi[x] < a + 0x1000 ? (g_keep_hi[x] - a + 3) / 4 : 1024;
		for (u32 k = lo; k < hi; k++) g_e->mask[k] = 1;
	}
}

/* g_e->mask[k] = 1 for the dwords of the page that belong to a live critical section: its
 * DebugInfo (+0) points to a debug record whose CriticalSection field (+4) points back */
static void rb_lock_mask(u32 p)
{
	const u32 *page = (const u32 *)(p << PAGE_SHIFT);
	memset(g_e->mask, 0, sizeof g_e->mask);
	for (u32 k = 0; k < 1024; k++) {
		u32 d = page[k];
		if ((d & 3) || d < 0x10000 || k + 3 >= 1024) continue;
		/* it must look like one: a free/held LockCount, a small RecursionCount, the owner a
		 * thread id or 0. A two-way list node ({next, prev}) passes the pointer test below, so
		 * 24 bytes of every such node were never put back and the lists came apart. Tested
		 * first, from the page alone: the pointer test is a system call, and floats that look
		 * like pointers made a restore cost tens of thousands of them (a visible hitch) */
		int lock = (int)page[k + 1];
		if (lock < -0x10000 || lock > 0x10000 || page[k + 2] > 0x10000 || page[k + 3] > 0x100000) continue;
		if (!rb_readable(d) || !rb_readable(d + 7)) continue;
		if (*(const u32 *)(d + 4) != (u32)(page + k)) continue;
		if (*(const u16 *)d != 0) continue;   /* debug record Type: RTL_CRITSECT_TYPE */
		for (u32 n = k; n < k + 6 && n < 1024; n++) g_e->mask[n] = 1;   /* 24 bytes */
	}
	if (rb_wine_segment(p << PAGE_SHIFT, page))
		for (u32 k = 0; k < 8; k++) g_e->mask[k] = 1;
	rb_keep_mask(p);
}

static void rb_restore_page(u32 i, const u8 *from)
{
	Engine *e = g_e;
	u32 p = e->pages[i], a = p << PAGE_SHIFT;
	u32 *page = (u32 *)a;
	const u32 *src = (const u32 *)from;
	rb_lock_mask(p);
	if (rb_wine_segment(a, page) && (page[3] != src[3] || page[4] != src[4]) && e->seg_kept++ < 8)
		log_msg("rollback: heap segment %08x's links kept in a restore: next %08x prev %08x, the snapshot had %08x %08x",
		        a, page[3], page[4], src[3], src[4]);
	if (e->flag[p] & PF_SHARED) {   /* only what ticks alone wrote */
		const u8 *m = e->omask + i * 128, *t = e->tmask + i * 128;
		for (u32 k = 0; k < 1024; k++)
			if ((m[k >> 3] & (1 << (k & 7))) || !(t[k >> 3] & (1 << (k & 7)))) e->mask[k] = 1;
	}
	for (u32 x = 0; x < e->nex; x++) {   /* SS2's sound state */
		if (e->ex_hi[x] <= a || e->ex_lo[x] >= a + 0x1000) continue;
		u32 lo = e->ex_lo[x] > a ? (e->ex_lo[x] - a) / 4 : 0, hi = e->ex_hi[x] < a + 0x1000 ? (e->ex_hi[x] - a + 3) / 4 : 1024;
		for (u32 k = lo; k < hi; k++) e->mask[k] = 1;
	}
	for (u32 k = 0; k < 1024; k++)
		if (!e->mask[k]) page[k] = src[k];
}

static void rb_restore(u32 slot)
{
	Engine *e = g_e;
	rb_rd_forget();
	/* a page as it was needs nothing put back: most known pages don't change from one tick to the
	 * next, and the per-page work (live locks, masks) on all of them made a rollback cost 14 ms
	 * late in a match on the Windows joiner, one per tick while the host turned the camera */
	for (u32 i = 0; i < e->npages; i++)
		if (rb_kept(e->pages[i]) && memcmp_pages((const u8 *)(e->pages[i] << PAGE_SHIFT), slot_page(slot, i)))
			rb_restore_page(i, slot_page(slot, i));
	e->endc_ok = 0;
}

/* The page differs from a copy, live critical sections left out */
static int rb_page_differs(u32 p, const u8 *want)
{
	const u32 *now = (const u32 *)(p << PAGE_SHIFT), *w = (const u32 *)want;
	if (!memcmp_pages((const u8 *)now, want)) return 0;
	rb_rd_forget();
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
		if ((e->flag[p] & (PF_SHARED | PF_WATCH)) || !(e->flag[p] & PF_CAND)) continue;
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

/* A known page that is copied, put back and compared. A shared page is put back from the
 * start, but only in the dwords ticks were seen changing and never seen changing between ticks
 * (rb_restore_page): it used to wait 60 ticks of watching first, and a rollback in those ticks
 * (one comes every few ticks with the other player's keys) left the page in the future. */
static int rb_kept(u32 p)
{
	Engine *e = g_e;
	if (!(e->flag[p] & PF_CAND)) return 0;   /* Oni freed it (rb_forget) */
	if ((e->flag[p] & PF_SHARED) && !e->index[p]) return 0;
	return page_writable(p);
}

/* ---- Memory Oni takes during a match ------------------------------------------------------ *
 * The candidates were the pages committed when the engine started. Memory the game commits
 * later (a heap growing into its reserved space, a new heap segment, a new small-block region)
 * was never protected, learned or put back, so it stayed in the future through every rollback.
 * Particles live in 50000-byte malloc'd blocks (0x455d00) added as a match needs more, and a
 * rewound class list (*0x65c5b0) meeting particles left in the future looped forever in the
 * particle update (0x45cc70): the Windows joiner froze at tick 249. Oni.exe's imports of
 * HeapAlloc, HeapReAlloc and VirtualAlloc are wrapped: memory the main thread gets in pages
 * that aren't candidates adds its allocation to Oni's, and before the next tick rb_grow makes
 * every committed page of those allocations a candidate. Pages taken in the tick before are
 * learned with their content then; older snapshots get that content too, but at those ticks
 * the game's own tables (rewound with them) didn't use that memory yet. */
#define IAT_HeapAlloc     0x529160
#define IAT_HeapReAlloc   0x52915c
#define IAT_VirtualAlloc  0x529060
#define IAT_HeapFree      0x529164
#define IAT_VirtualFree   0x52906c
#define IAT_HeapSize      0x529058

static void *(WINAPI *g_heap_alloc)(HANDLE, u32, u32);
static void *(WINAPI *g_heap_realloc)(HANDLE, u32, void *, u32);
static void *(WINAPI *g_virtual_alloc)(void *, u32, u32, u32);
static int (WINAPI *g_heap_free)(HANDLE, u32, void *);
static int (WINAPI *g_virtual_free)(void *, u32, u32);
static u32 (WINAPI *g_heap_size)(HANDLE, u32, const void *);
static u32 rb_alloc_of(const void *p);
static void rb_freed(u32 base);

/* ---- Oni's big blocks during a match ------------------------------------------------------- *
 * A rollback can't undo the heap taking a new segment. The Linux host of 2026-11-08 crashed in
 * HeapFree (ntdll, writing 032f64f4) while the level unloaded after a match: at tick 11153 Oni's C
 * runtime heap took a new 64 KB segment (032e0000) for a 50000-byte particle block (0x455d00); a
 * rollback put the heap's own bookkeeping back to before that tick, so the heap no longer knew the
 * segment, and the re-run took another one (032f0000, logged at the same tick). From then on the
 * heap's lists didn't match its memory, and freeing everything at the unload went through them.
 * (Growing the heap before the match doesn't work under Wine: a block shrunk in place keeps its
 * room, and an emptied segment is given back.)
 * So while the engine runs, the main thread's HeapAlloc of POOL_MIN..POOL_SLOT bytes on Oni's
 * heap takes a slot of Arklite's pool instead: POOL_SLOTS slots of POOL_SLOT bytes, allocated once
 * and never freed, game memory like Oni's (rb_start). Which slots are taken, and how big each
 * block is, is tick state (.tstate): a rollback puts it back with the blocks' contents, and the
 * heap itself never changes for them. HeapFree, HeapReAlloc and HeapSize know a slot's address
 * (any thread, any time, after the match too: the level's unload frees them). A full pool falls
 * back to the heap, logged. */
#define POOL_SLOT         0x10000
#define POOL_MIN          0x4000
#define HEAP_ZERO_MEMORY  0x08
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10

static u32 g_pool_size[POOL_SLOTS] TICK_STATE;   /* the block's bytes, 0: free */
static u32 g_pool_full_logs, g_pool_logs;

/* Its slot if p is a pool block, -1 if not the pool's at all */
static int pool_slot(const void *p)
{
	u32 o = (u32)p - (u32)g_pool;
	if (!g_pool || (u32)p < (u32)g_pool || o >= POOL_SLOTS * POOL_SLOT || (o & (POOL_SLOT - 1))) return -1;
	return (int)(o / POOL_SLOT);
}

/* any: a block of any size up to a slot (one that made the heap grow, rb_heap_alloc) */
static void *pool_alloc(HANDLE h, u32 flags, u32 n, int any)
{
	Engine *e = g_e;
	u32 used = 0;
	int free_slot = -1;
	/* only inside a tick: its slot table is rewound to a tick's start, so a block taken between
	 * ticks would be forgotten by a rollback while still in use (one freed between ticks is only
	 * kept: a leak, not a clash) */
	if (!g_pool || h != ONgCrtHeap || (n < POOL_MIN && !any) || !n || n > POOL_SLOT || !e || !e->active || !e->in_body ||
	    GetCurrentThreadId() != e->main_tid)
		return 0;
	for (int i = 0; i < POOL_SLOTS; i++) {
		if (g_pool_size[i]) used++;
		else if (free_slot < 0) free_slot = i;
	}
	if (free_slot < 0) {
		if (g_pool_full_logs++ < 4)
			log_msg("rollback: tick %d: the pool for Oni's big blocks is full (%d): a %d-byte block comes from the heap, which may "
			        "grow during the match", e->last_tick, POOL_SLOTS, n);
		return 0;
	}
	if (used + 1 > g_pool_used_max) g_pool_used_max = used + 1;
	if (g_pool_logs < 4 && ++g_pool_logs)
		log_msg("rollback: tick %d: Oni's %d-byte block from the pool (slot %d, %d in use)", e->last_tick, n, free_slot, used + 1);
	g_pool_size[free_slot] = n;
	u8 *p = g_pool + free_slot * POOL_SLOT;
	if (flags & HEAP_ZERO_MEMORY) memset(p, 0, n);
	return p;
}

static void rb_new_memory(const void *p, u32 n)
{
	Engine *e = g_e;
	if (!p || !n || !e || !e->active || GetCurrentThreadId() != e->main_tid) return;
	if ((e->flag[(u32)p >> PAGE_SHIFT] & PF_CAND) && (e->flag[((u32)p + n - 1) >> PAGE_SHIFT] & PF_CAND)) return;
	rb_add_oni(p);
	e->grow = 1;
}

/* In a tick, the heap gave memory it didn't have before (a new segment, or more of one committed) */
static int rb_heap_grew(const void *p, u32 n)
{
	Engine *e = g_e;
	if (!p || !n || !e || !e->active || !e->in_body || GetCurrentThreadId() != e->main_tid) return 0;
	return !(e->flag[(u32)p >> PAGE_SHIFT] & PF_CAND) || !(e->flag[((u32)p + n - 1) >> PAGE_SHIFT] & PF_CAND);
}

static u32 g_grow_logs;

static void *WINAPI rb_heap_alloc(HANDLE h, u32 flags, u32 n)
{
	void *p = pool_alloc(h, flags, n, 0), *q;
	if (p) return p;
	p = g_heap_alloc(h, flags, n);
	/* the heap grew for it: given back at once (the heap as it was), and a pool slot instead. Any
	 * size: the bot test of 2026-11-08 grew the heap at tick 4428 with no 16-64 KB block at all */
	if (h == ONgCrtHeap && rb_heap_grew(p, n) && (q = pool_alloc(h, flags, n, 1))) {
		u32 at = (u32)p;
		g_heap_free(h, 0, p);
		if (g_grow_logs++ < 8)
			log_msg("rollback: tick %d: a %d-byte block of Oni's would have made its heap grow (at %08x): given back, from the "
			        "pool instead", g_e->last_tick, n, at);
		return q;
	}
	if (h == ONgCrtHeap && rb_heap_grew(p, n) && g_grow_logs++ < 8)
		log_msg("rollback: tick %d: a %d-byte block of Oni's made its heap grow (at %08x) and the pool can't take it: a rollback "
		        "past this tick may break the heap", g_e->last_tick, n, (u32)p);
	rb_new_memory(p, n);
	return p;
}

static void *WINAPI rb_heap_realloc(HANDLE h, u32 flags, void *old, u32 n)
{
	int slot = pool_slot(old);
	if (slot >= 0) {   /* a pool block: in its slot while it fits, else to the heap */
		u32 had = g_pool_size[slot];
		void *p;
		if (n <= POOL_SLOT) {
			if ((flags & HEAP_ZERO_MEMORY) && n > had) memset((u8 *)old + had, 0, n - had);
			g_pool_size[slot] = n ? n : 1;
			return old;
		}
		if (flags & HEAP_REALLOC_IN_PLACE_ONLY) return 0;
		if (!(p = g_heap_alloc(h, flags & ~HEAP_ZERO_MEMORY, n))) return 0;
		memcpy(p, old, had);
		if (flags & HEAP_ZERO_MEMORY) memset((u8 *)p + had, 0, n - had);
		g_pool_size[slot] = 0;
		rb_new_memory(p, n);
		return p;
	}
	u32 base = rb_alloc_of(old);
	void *p = g_heap_realloc(h, flags, old, n), *q;
	rb_freed(base);
	if (h == ONgCrtHeap && rb_heap_grew(p, n) && (q = pool_alloc(h, flags, n, 1))) {   /* as in rb_heap_alloc */
		u32 at = (u32)p;
		memcpy(q, p, n);
		g_heap_free(h, 0, p);
		if (g_grow_logs++ < 8)
			log_msg("rollback: tick %d: resizing a block of Oni's to %d bytes would have made its heap grow (at %08x): given "
			        "back, from the pool instead", g_e->last_tick, n, at);
		return q;
	}
	rb_new_memory(p, n);
	return p;
}

static void *WINAPI rb_virtual_alloc(void *at, u32 n, u32 type, u32 protect)
{
	void *p = g_virtual_alloc(at, n, type, protect);
	if (type & MEM_COMMIT) rb_new_memory(p, n);
	return p;
}

/* An allocation of Oni's that was freed: its pages stop being the game's at once. The address
 * can be handed to anything next: a large decode buffer (its own region under Wine) freed after
 * a long sound, then reused for a piece of Wine's process heap, was kept being put back, and the
 * rewound heap crashed the Linux host in acmStreamOpen. */
static void rb_forget(u32 base)
{
	Engine *e = g_e;
	u32 k = 0, n = 0;
	while (k < e->noni && e->oni[k] != base) k++;
	if (k == e->noni) return;
	e->noni--;
	e->oni[k] = e->oni[e->noni];
	e->oni_span[k] = e->oni_span[e->noni];
	for (u32 i = 0; i < e->nregs; i++) {
		Region *r = &e->regs[i];
		if (r->alloc != base || !r->size) continue;
		memset(e->flag + (r->base >> PAGE_SHIFT), 0, r->size >> PAGE_SHIFT);
		n += r->size >> PAGE_SHIFT;
		r->size = 0;
	}
	e->freed += n;
	if (n && e->freed_logs++ < 8)
		log_msg("rollback: tick %d: Oni gave back %d KB at %08x; out of the snapshots (%d KB so far)",
		        e->last_tick, n * 4, base, e->freed * 4);   /* not the game state's tick: unloading frees it */
}

/* Still an allocation with that base. Its first page needn't be committed: on Windows some
 * live ones start reserved, and taking that for freed dropped 16 MB of the joiner's game state
 * at tick 0 (a desync at tick 1740) */
static int rb_alloc_alive(u32 base)
{
	MEMORY_BASIC_INFORMATION m;
	return VirtualQuery((const void *)base, &m, sizeof m) == sizeof m && m.State != 0x10000 /* MEM_FREE */ &&
	       (u32)m.AllocationBase == base;
}

/* The allocation p is in, if the snapshots follow it */
static u32 rb_alloc_of(const void *p)
{
	MEMORY_BASIC_INFORMATION m;
	Engine *e = g_e;
	if (!p || !e || !e->active || GetCurrentThreadId() != e->main_tid || !(e->flag[(u32)p >> PAGE_SHIFT] & PF_CAND)) return 0;
	if (VirtualQuery(p, &m, sizeof m) != sizeof m) return 0;
	return (u32)m.AllocationBase;
}

static void rb_freed(u32 base)
{
	if (base && !rb_alloc_alive(base)) rb_forget(base);
}

static u32 WINAPI rb_heap_size(HANDLE h, u32 flags, const void *p)
{
	int slot = pool_slot(p);
	return slot >= 0 ? g_pool_size[slot] : g_heap_size(h, flags, p);
}

static int WINAPI rb_heap_free(HANDLE h, u32 flags, void *p)
{
	int slot = pool_slot(p);
	if (slot >= 0) {
		g_pool_size[slot] = 0;
		return 1;
	}
	u32 base = rb_alloc_of(p);
	int r = g_heap_free(h, flags, p);
	rb_freed(base);
	return r;
}

static int WINAPI rb_virtual_free(void *p, u32 n, u32 type)
{
	u32 base = rb_alloc_of(p);
	int r = g_virtual_free(p, n, type);
	rb_freed(base);
	return r;
}

/* Allocations another thread freed, or freed in ways not seen above (the free hooks only act
 * on the main thread). One whose address was taken again by the time of this check (same base,
 * another size: under Wine a piece of its process heap) is dropped too: kept, its pages would be
 * put back over the new owner's */
static void rb_check_allocs(void)
{
	Engine *e = g_e;
	for (u32 k = e->noni; k-- > 0; ) {
		u32 base = e->oni[k], span;
		if (!rb_alloc_alive(base)) {
			rb_forget(base);
		} else if ((span = rb_span(base)) != e->oni_span[k]) {
			if (e->reused++ < 8)
				log_msg("rollback: tick %d: Oni's allocation at %08x was freed and its address taken again (%d KB, was %d KB); "
				        "out of the snapshots", e->last_tick, base, span / 1024, e->oni_span[k] / 1024);
			rb_forget(base);
		}
	}
}

static void rb_hook_iat(u32 slot, const u8 *site, void *hook, void **orig)
{
	if (!code_matches((u32)site, (const u8[]){ 0xff, 0x15 }, 2) || *(const u32 *)(site + 2) != slot) {
		log_msg("rollback: Oni.exe's import at 0x%x isn't called at 0x%x: memory it gives isn't followed", slot, (u32)site);
		return;
	}
	*orig = *(void **)slot;
	patch_write(slot, &hook, 4);
}

static void rb_hook_alloc(void)
{
	static int done;
	if (done++) return;
	rb_hook_iat(IAT_HeapAlloc, (const u8 *)0x51fc90, (void *)rb_heap_alloc, (void **)&g_heap_alloc);
	rb_hook_iat(IAT_HeapReAlloc, (const u8 *)0x51fd88, (void *)rb_heap_realloc, (void **)&g_heap_realloc);
	rb_hook_iat(IAT_VirtualAlloc, (const u8 *)0x524515, (void *)rb_virtual_alloc, (void **)&g_virtual_alloc);
	rb_hook_iat(IAT_HeapFree, (const u8 *)0x51fc1c, (void *)rb_heap_free, (void **)&g_heap_free);
	rb_hook_iat(IAT_HeapSize, (const u8 *)0x52521e, (void *)rb_heap_size, (void **)&g_heap_size);
	/* the pool only works when every heap call Oni makes knows it */
	if (g_heap_alloc && g_heap_realloc && g_heap_free && g_heap_size)
		g_pool = VirtualAlloc(0, POOL_SLOTS * POOL_SLOT, 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 4);
	log_msg(g_pool ? "rollback: Oni's big blocks (%d-%d KB) come from Arklite's pool during a match (%d slots): its heap never "
	                 "grows then" : "rollback: no pool for Oni's big blocks: its heap may grow during a match",
	        POOL_MIN / 1024, POOL_SLOT / 1024, POOL_SLOTS);
	if (code_matches(0x5240aa, (const u8[]){ 0x8b, 0x3d }, 2) && *(const u32 *)0x5240ac == IAT_VirtualFree) {
		g_virtual_free = *(void **)IAT_VirtualFree;
		void *hook = (void *)rb_virtual_free;
		patch_write(IAT_VirtualFree, &hook, 4);
	} else {
		log_msg("rollback: Oni.exe's VirtualFree import isn't where it was: memory it frees is dropped a tick late");
	}
}

/* Every committed page of Oni's allocations becomes a candidate (protected from this tick on) */
static void rb_grow(void)
{
	Engine *e = g_e;
	MEMORY_BASIC_INFORMATION m;
	u32 added = 0, first = 0;

	e->grow = 0;
	for (u32 k = 0; k < e->noni; k++) {
		u32 base = e->oni[k];
		for (u32 a = base; a < 0x7ff00000u && VirtualQuery((const void *)a, &m, sizeof m) == sizeof m &&
		     (u32)m.AllocationBase == base && m.RegionSize; a = (u32)m.BaseAddress + m.RegionSize) {
			if (!writable(&m) || m.Type == MEM_MAPPED) continue;
			u32 p = (u32)m.BaseAddress >> PAGE_SHIFT, end = p + (m.RegionSize >> PAGE_SHIFT);
			while (p < end) {
				while (p < end && (e->flag[p] & (PF_CAND | PF_FOREIGN))) p++;
				u32 q = p;
				while (q < end && !(e->flag[q] & (PF_CAND | PF_FOREIGN))) q++;
				if (q > p && e->nregs < MAX_REGIONS) {
					Region *r = &e->regs[e->nregs++];
					r->base = p << PAGE_SHIFT;
					r->size = (q - p) << PAGE_SHIFT;
					r->alloc = base;
					r->type = m.Type;
					r->protect = m.Protect;
					memset(e->flag + p, PF_CAND | (m.Protect & 0xf0 ? PF_EXEC : 0), q - p);
					if (!first) first = p << PAGE_SHIFT;
					added += q - p;
				}
				p = q;
			}
		}
	}
	e->grown += added;
	if (added && e->grow_logs++ < 8)
		log_msg("rollback: tick %d: Oni took %d KB more (from %08x); in the snapshots now (%d KB since the match began)",
		        e->last_tick, added * 4, first, e->grown * 4);
}

/* Every page of a heap that isn't Oni's C runtime heap (the process heap, msacm32's, a driver's)
 * leaves the snapshots for good. Such a page can only be a candidate through an address Oni
 * freed and the system reused, and putting it back rewinds that heap's own bookkeeping: under
 * Wine a one-shot sound opening its ACM stream (0x484b70 -> acmStreamOpen) then faulted in the
 * process heap (the Linux host's crash after a mortar hit, and its freeze at tick 7735). Walks
 * every heap: at the first tick, after Oni took memory, and once a second. */
#define PROCESS_HEAP_REGION 1

static void rb_heap_range(HANDLE h, u32 lo, u32 n, const char *kind)
{
	Engine *e = g_e;
	u32 dropped = 0, first = 0, known = 0;
	if (!n) n = 1;
	for (u32 p = lo >> PAGE_SHIFT; p <= (lo + n - 1) >> PAGE_SHIFT && p < PAGES; p++) {
		if (!(e->flag[p] & PF_CAND)) continue;
		if (e->index[p]) known++;
		e->flag[p] = PF_FOREIGN;   /* not a candidate, and never protected (rb_protect skips it) */
		if (e->ndropped < RB_MAX_DROPPED) e->dropped[e->ndropped++] = p;
		if (!dropped++) first = p << PAGE_SHIFT;
	}
	e->heap_pages += dropped;
	if (dropped && e->heap_logs++ < 8) {
		/* whose allocation the page is in, and whether Oni's runtime made it: a page of one of
		 * Oni's live allocations that another heap's walk claims is game memory left in the future */
		MEMORY_BASIC_INFORMATION m;
		u32 base = VirtualQuery((const void *)first, &m, sizeof m) == sizeof m ? (u32)m.AllocationBase : 0, k = 0;
		while (k < e->noni && e->oni[k] != base) k++;
		log_msg("rollback: tick %d: %d snapshot page(s) from %08x (%d already written in ticks) belong to heap %08x (%s "
		        "%08x, %d KB; allocation %08x, %s), not Oni's; out of the snapshots", e->last_tick, dropped, first, known,
		        (u32)h, kind, lo, n / 1024, base, k < e->noni ? "one of Oni's" : "not one of Oni's");
	}
}

static void rb_heaps_guard(void)
{
	Engine *e = g_e;
	HANDLE heaps[MAX_HEAPS], crt = ONgCrtHeap;
	u32 n = GetProcessHeaps(MAX_HEAPS, heaps), walked = 0;
	if (n > MAX_HEAPS) n = MAX_HEAPS;
	for (u32 i = 0; i < n; i++) {
		PROCESS_HEAP_ENTRY en;
		u32 rlo = 0, rhi = 0;
		if (!heaps[i] || heaps[i] == crt) continue;
		en.lpData = 0;
		while (HeapWalk(heaps[i], &en)) {
			u32 a = (u32)en.lpData;
			walked++;
			if (en.wFlags & PROCESS_HEAP_REGION) {   /* a whole segment: its blocks need no look */
				rlo = a;
				rhi = a + en.more[0] + en.more[1];
				rb_heap_range(heaps[i], rlo, rhi - rlo, "segment");
			} else if (a < rlo || a >= rhi) {        /* a large block, outside every segment */
				rb_heap_range(heaps[i], a, en.cbData + en.cbOverhead, "large block");
			}
		}
	}
	if (!e->heap_walked) log_msg("rollback: guarding against other heaps: %d heaps, %d entries walked", n, walked);
	e->heap_walked = walked;
}

/* ---- Particle lists ------------------------------------------------------------------------ *
 * Each particle class (0x65c520 + i * 0x98, *0x6a8666 of them) keeps its live particles in a
 * list at +0x90, linked through +0x34. One that loops or points at unreadable memory freezes or
 * crashes the particle update, so at each tick's start every list is checked: a broken one is
 * logged with the pages its particles are on (whether the snapshots cover them) and cut, so
 * the game goes on (both PCs' particles may differ from then on). */
#define P3gClassCount  (*(volatile u16 *)0x6a8666)
#define P3gClasses     0x65c520
#define P3_CLASS_SIZE  0x98
#define P3_MAX_CLASSES 2048

static int rb_node_ok(u32 n) { return !(n & 3) && rb_readable(n) && rb_readable(n + 0x37); }
static u32 rb_next(u32 n) { return *(const u32 *)(n + 0x34); }

static void rb_page_desc(u32 a, char *buf)
{
	Engine *e = g_e;
	u32 p = a >> PAGE_SHIFT;
	u8 f = p < PAGES ? e->flag[p] : 0;
	u16 ix = p < PAGES ? e->index[p] : 0;
	fmt(buf, " %08x (%s%s%s)", a, !(f & PF_CAND) ? "not in the snapshots" : ix ? "known" : "never written in a tick",
	    f & PF_SHARED ? ", shared" : "", f & PF_FOREIGN ? ", foreign" : "");
}

static void rb_particles_check(u32 tick)
{
	Engine *e = g_e;
	u32 n = P3gClassCount;
	if (n > P3_MAX_CLASSES) n = P3_MAX_CLASSES;
	rb_rd_forget();
	for (u32 c = 0; c < n; c++) {
		u32 *head = (u32 *)(P3gClasses + c * P3_CLASS_SIZE + 0x90);
		u32 power = 1, lam = 1, tort = *head, prev = 0, hare = 0, bad = 0;
		if (!tort) continue;
		if (!rb_node_ok(tort)) {
			bad = tort;
			*head = 0;
		} else {
			prev = tort;
			hare = rb_next(tort);
			while (hare && hare != tort) {
				if (!rb_node_ok(hare)) {
					bad = hare;
					*(u32 *)(prev + 0x34) = 0;
					break;
				}
				if (power == lam) {
					tort = hare;
					power <<= 1;
					lam = 0;
				}
				prev = hare;
				hare = rb_next(hare);
				lam++;
			}
			if (!bad && !hare) continue;
		}
		e->plist_cuts++;
		if (e->plist_logs >= 6) continue;
		e->plist_logs++;
		if (bad) {
			char d[96];
			rb_page_desc(bad, d);
			log_msg("rollback: tick %d: particle class %d's list points at unreadable memory%s; cut there", tick, c, d);
			continue;
		}
		/* a loop of lam particles: find where it starts, cut it before it comes round again */
		u32 a = *head, b = *head, desc_len = 0;
		char desc[400];
		for (u32 i = 0; i < lam; i++) b = rb_next(b);
		while (a != b) {
			a = rb_next(a);
			b = rb_next(b);
		}
		desc[0] = 0;
		b = a;
		for (u32 i = 0; i + 1 < lam; i++) {
			if (i < 4) {
				rb_page_desc(b, desc + desc_len);
				while (desc[desc_len]) desc_len++;
			}
			b = rb_next(b);
		}
		*(u32 *)(b + 0x34) = 0;
		log_msg("rollback: tick %d: particle class %d's list loops (%d particles round, from %08x); cut. Particles:%s",
		        tick, c, lam, a, desc);
	}
}

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
	rb_rd_forget();
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
		char path[MAX_PATH];
		e->out = g_out = log_file_create("Arklite-rollbacktest.txt", path);
		out("Arklite rollback test: each sample puts the game back %d ticks, runs them again with their", RB_BACK);
		out("inputs and compares every known page with the first time. Oni's C runtime heap: 0x%08x.", (u32)ONgCrtHeap);
	}

	rb_game_capture();
	if (SSgLock) SSrLock(SSgLock);
	rb_sound_before_restore(back);
	rb_restore(back % RB_SLOTS);
	if (SSgLock) SSrUnlock(SSgLock);
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
	rb_check_allocs();
	{
		u32 grew = e->grow;
		if (grew) rb_grow();
		if (grew || !e->heap_walked || !(e->ticks_protected % 60)) rb_heaps_guard();
	}
	rb_particles_check(tick);
	rb_snapshot(tick, action);
	if (e->samples_done < e->samples_wanted && tick >= e->next_sample) {
		e->next_sample = tick + SURVEY_EVERY;
		rb_rewind_test(tick, body);
	}
	if (e->ticks_protected < 3) log_msg("rollback: tick %d: protecting %d known pages' neighbours", tick, e->npages);
	rb_protect();
	e->body_serial++;
	e->amb_lo[e->cur] = SSgAmbientNextId;
	e->in_body = 1;
	body(action);
	e->in_body = 0;
	e->amb_hi[e->cur] = SSgAmbientNextId;
	rb_unprotect();
	/* shared pages: what this tick changed (from its start, in its slot), and the end for the
	 * next tick's start to compare with */
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		e->endc_have[i] = 0;
		if (!(e->flag[p] & PF_SHARED) || !page_writable(p)) continue;
		const u32 *now = (const u32 *)(p << PAGE_SHIFT), *was = (const u32 *)slot_page(e->cur, i);
		u8 *t = e->tmask + i * 128;
		for (u32 k = 0; k < 1024; k++)
			if (now[k] != was[k]) t[k >> 3] |= (u8)(1 << (k & 7));
		memcpy(e->endc + i * 0x1000, now, 0x1000);
		e->endc_have[i] = 1;
	}
	e->endc_ok = 1;
	while (e->notes_logged < e->nnotes) {
		u32 k = e->notes_logged++;
		char near[160];
		rb_globals_near(e->note_page[k], near);
		log_msg("rollback: page %08x is shared with thread %d (another thread uses it); Oni.exe globals near:%s",
		        e->note_page[k] << PAGE_SHIFT, e->note_tid[k], near[0] ? near : " none");
	}
	e->ticks_protected++;
	if (e->rescues && e->ticks_protected <= 3)
		log_msg("rollback: tick %d finished after the watchdog stepped in", tick);
	if (e->ticks_protected <= 3 || !(e->ticks_protected % 600))
		log_msg("rollback: tick %d done (%d protected): %d known pages, %d shared, %d foreign, %d over the limit, %d runs",
		        tick, e->ticks_protected, e->npages, e->nshared, e->nforeign_pages, e->overflow, e->last_runs);
	if (e->ticks_protected <= RB_WATCH_FIRST || !(e->ticks_protected % RB_WATCH_EVERY)) rb_watch();
	return 1;
}

/* ---- The watchdog ------------------------------------------------------------------------ *
 * A write the game makes from inside a system call doesn't fault: the call fails instead. Under
 * Wine (new WoW64 mode) that is every kernel32/ntdll call that stores into the caller's buffer
 * (a read into a heap buffer, a time stamp into an Oni.exe global), so a buffer on a
 * write-protected page can make a tick wait forever on a call that never succeeds; the first
 * networked rollback test froze the Linux host in tick 0 that way, while Windows ran on.
 * So while a tick runs with protection on, this thread checks on it every 200 ms. A tick still
 * running after 1.5 s: the main thread is suspended, its EIP and the Oni.exe/Arklite return
 * addresses on its stack go to Arklite.log, and every protected page that its registers or its
 * stack point into is learned (as a first write would: the page is copied into the snapshots
 * and opened), so the call can succeed. Still running 1.5 s later: every page is opened for
 * the rest of that tick (pages it writes then aren't learned; logged). */
#define RB_DOG_MS 1500

static int rb_learn_at(u32 a)
{
	Engine *e = g_e;
	u32 p = a >> PAGE_SHIFT, old;
	if (p >= PAGES || !(e->flag[p] & PF_PROT) || e->index[p] || (e->flag[p] & PF_FOREIGN)) return 0;
	if (e->npages >= RB_MAX_PAGES || __sync_lock_test_and_set(&e->learn_lock, 1)) return 0;
	rb_learn(p);
	rb_unlock();
	VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old);
	e->flag[p] &= ~PF_PROT;
	return 1;
}

static int rb_in_image(u32 a, u32 base)
{
	u32 pe = *(u32 *)(base + 0x3c), size = *(u32 *)(base + pe + 24 + 56);   /* SizeOfImage */
	return a >= base + 0x1000 && a < base + size;
}

static void rb_dog_report(u32 stage, u32 ms, const u32 *ctx, u32 learned)
{
	Engine *e = g_e;
	char mod[128] = "?", calls[400];
	MEMORY_BASIC_INFORMATION m;
	u32 eip = ctx[0xb8 / 4], esp = ctx[0xc4 / 4], n = 0, len = 0;
	const char *name = mod;

	if (VirtualQuery((const void *)eip, &m, sizeof m) == sizeof m && GetModuleFileNameA(m.AllocationBase, mod, sizeof mod)) {
		for (const char *c = mod; *c; c++)
			if (*c == '\\' || *c == '/') name = c + 1;
	}
	calls[0] = 0;
	for (u32 a = esp; a + 4 <= e->stack_top && a < esp + 0x10000 && n < 12; a += 4) {
		u32 v = *(const u32 *)a;
		if (!rb_in_image(v, 0x400000) && !rb_in_image(v, (u32)&__ImageBase)) continue;
		fmt(calls + len, " %08x", v);
		while (calls[len]) len++;
		n++;
	}
	log_msg("rollback: watchdog: tick %d has run %d ms (stage %d): main thread at %08x (%s, base %08x), esp %08x; "
	        "return addresses on its stack:%s", GS_TICK(ONgGameState), ms, stage, eip, name,
	        (u32)m.AllocationBase, esp, n ? calls : " none");
	if (stage == 1)
		log_msg("rollback: watchdog: learned %d protected page(s) the main thread's registers and stack point into", learned);
	else
		log_msg("rollback: watchdog: opened every protected page for the rest of this tick (%d runs); writes in it "
		        "aren't learned, so a rollback past this tick can come out different", e->nruns);
}

static u32 WINAPI rb_watchdog(void *unused)
{
	u32 serial = 0, since = 0, stage = 0;
	u32 ctx[0x2cc / 4];

	for (;;) {
		Engine *e = g_e;
		Sleep(200);
		if (!e || !e->active || !e->in_body || !e->main_thread) {
			stage = 0;
			continue;
		}
		u32 now = GetTickCount();
		if (e->body_serial != serial) {
			serial = e->body_serial;
			since = now;
			stage = 0;
			continue;
		}
		if (stage >= 2 || now - since < RB_DOG_MS * (stage + 1)) continue;
		if (SuspendThread(e->main_thread) == 0xffffffffu) continue;
		if (e->in_fault || !e->in_body || e->body_serial != serial) {   /* moving after all */
			ResumeThread(e->main_thread);
			continue;
		}
		memset(ctx, 0, sizeof ctx);
		ctx[0] = 0x10007;   /* CONTEXT_FULL */
		int have = GetThreadContext(e->main_thread, ctx);
		u32 learned = 0;
		stage++;
		e->rescues++;
		if (stage == 1 && have) {
			for (u32 k = 0x9c / 4; k <= 0xb4 / 4; k++) {   /* edi esi ebx edx ecx eax ebp */
				learned += rb_learn_at(ctx[k]);
				learned += rb_learn_at(ctx[k] + 63);
			}
			for (u32 a = ctx[0xc4 / 4] & ~3u; a + 4 <= e->stack_top && a < ctx[0xc4 / 4] + 0x10000; a += 4) {
				u32 v = *(const u32 *)a;
				learned += rb_learn_at(v);
				learned += rb_learn_at(v + 63);
			}
		} else {
			for (u32 i = 0; i < e->nruns; i++) {
				u32 old;
				VirtualProtect((void *)(e->runs[i].page << PAGE_SHIFT), e->runs[i].n << PAGE_SHIFT, e->runs[i].old, &old);
				for (u32 k = e->runs[i].page; k < e->runs[i].page + e->runs[i].n; k++) e->flag[k] &= ~PF_PROT;
			}
		}
		e->rescue_learned += learned;
		ResumeThread(e->main_thread);
		if (have) rb_dog_report(stage, now - since, ctx, learned);
		else log_msg("rollback: watchdog: tick stuck, but the main thread's registers can't be read (stage %d)", stage);
	}
	return 0;
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
	g_e->omask = alloc_own(RB_MAX_PAGES * 128);
	g_e->tmask = alloc_own(RB_MAX_PAGES * 128);
	g_e->endc = alloc_own(RB_MAX_PAGES * 0x1000);
	if (!g_e->flag || !g_e->index || !g_e->regs || !g_e->gimg || !g_e->gnow || !g_e->omask || !g_e->tmask || !g_e->endc) return 0;
	for (u32 s = 0; s < RB_SLOTS; s++)
		if (!(g_e->slot[s] = alloc_own(RB_MAX_PAGES * 0x1000))) return 0;
	CreateThread(0, 0x10000, rb_watchdog, 0, 0, 0);
	rb_hook_alloc();
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

/* The sound a rollback must not rewind. Ambient sounds (loops) the ticks being taken back
 * started are stopped: the game forgets their play ids, and before this they played on forever
 * (the SMG's firing loop). The re-run ticks start them again (rollback_impulses keeps one-shot
 * sounds, which end by themselves, from playing twice). SS2's tables and the channels they
 * point at go on the exclusion list, so the restore leaves them as the sound thread has them. */
static void rb_add_excl(u32 lo, u32 hi)
{
	Engine *e = g_e;
	if (e->nex < RB_MAX_EXCL && hi > lo) {
		e->ex_lo[e->nex] = lo;
		e->ex_hi[e->nex] = hi;
		e->nex++;
	}
}

/* An open ACM stream: its memory is msacm32's and its driver's, never the game's, but under
 * Wine nothing keeps it out of a page that was Oni's once (a freed block's address reused).
 * Put back, the sound thread converted through a stream whose driver data was gone: the Linux
 * host, a call to 0x273 and then to 0 in acmStreamConvert (0x48500c); Wine's ADPCM converter
 * calls through its instance data (aad->convert). The stream is left out of restores and logged
 * when it lies in a known page; under Wine (WINE_ACMSTREAM: +0 type 0x5eed0003, +8 the driver,
 * +0xc drvInst, whose dwDriver at +0x2c is the converter's data and has at +0x30 the stream
 * itself) so are its driver and the converter's data. */
#define WINE_ACMOBJ_STREAM 0x5eed0003

static void rb_acm_range(u32 lo, u32 n, const char *what)
{
	Engine *e = g_e;
	if (!lo || !rb_readable(lo) || !rb_readable(lo + n - 1)) return;
	rb_add_excl(lo, lo + n);
	for (u32 p = lo >> PAGE_SHIFT; p <= (lo + n - 1) >> PAGE_SHIFT; p++) {
		if (!e->index[p]) continue;
		e->acm_hits++;
		if (e->acm_logs++ < 8)
			log_msg("rollback: tick %d: an open ACM %s (%08x) lies in snapshot page %08x; left out of the restore",
			        e->last_tick, what, lo, p << PAGE_SHIFT);
	}
}

static void rb_acm_guard(u32 h)
{
	if (!h || !rb_readable(h)) return;
	rb_acm_range(h, 0x80, "stream");
	if (!rb_readable(h + 0x33)) return;
	const u32 *w = (const u32 *)h;
	if (w[0] != WINE_ACMOBJ_STREAM || w[12] != h) {   /* not Wine's (Windows has its own layout) */
		static int said;
		if (!said++) log_msg("rollback: an ACM stream (%08x) isn't Wine's layout (%08x, has %08x): only the stream is kept out", h, w[0], w[12]);
		return;
	}
	rb_acm_range(w[2], 0x40, "driver");
	rb_acm_range(w[11], 0x40, "converter's data");
}

static void rb_sound_before_restore(u32 tick)
{
	Engine *e = g_e;
	u32 *t = SSgAmbients, n = SSgAmbientCount;

	e->nex = 0;
	rb_add_excl(SSgGlobalsLo, SSgGlobalsHi);
	/* both channel arrays whole: a channel holds its DirectSound buffer (+0x58) and ACM stream
	 * (+0xb0, opened and closed by 0x484b70); one put back held a stream closed since, and
	 * opening the next one crashed in the heap (the Linux host at tick 18208) */
	if (SSgChannels1 && SSgChannelCount1 < 4096) rb_add_excl((u32)SSgChannels1, (u32)SSgChannels1 + SSgChannelCount1 * SS_CHANNEL_SIZE);
	if (SSgChannels2 && SSgChannelCount2 < 4096) rb_add_excl((u32)SSgChannels2, (u32)SSgChannels2 + SSgChannelCount2 * SS_CHANNEL_SIZE);
	for (u32 i = 0; SSgChannels1 && i < SSgChannelCount1 && i < 4096; i++) rb_acm_guard(*(u32 *)(SSgChannels1 + i * SS_CHANNEL_SIZE + 0xb0));
	for (u32 i = 0; SSgChannels2 && i < SSgChannelCount2 && i < 4096; i++) rb_acm_guard(*(u32 *)(SSgChannels2 + i * SS_CHANNEL_SIZE + 0xb0));
	/* SS2's lock and its list of playing channels (added at 0x4844f1, removed at 0x485a75): the
	 * lock put back from a snapshot let the sound thread and the main thread in at once, and the
	 * list put back didn't match the channels; the sound thread converted with an ACM stream the
	 * main thread had just closed (the Linux host, a call to 0 in acmStreamConvert at tick 64318).
	 * rb_lock_mask misses a lock it can't prove live (no debug record, or across two pages) */
	if (SSgLock) rb_add_excl((u32)SSgLock, (u32)SSgLock + 24);
	{
		u32 *l = SSgPlaying;
		if (l && rb_readable((u32)l) && rb_readable((u32)l + 0x13)) {
			rb_add_excl((u32)l, (u32)l + 0x14);
			if (l[4] && l[0] <= 64 && l[3] < 4096) rb_add_excl(l[4], l[4] + l[0] * l[3]);
		}
	}
	if (!t || n > 4096 || !rb_readable((u32)t)) return;
	rb_add_excl((u32)t, (u32)t + n * 0x58);
	for (u32 i = 0; i < n; i++) {
		u32 *a = t + i * 0x16;
		if (!a[1]) continue;
		for (u32 s = 0; s < RB_SLOTS; s++) {
			if (!e->slot_valid[s] || e->slot_tick[s] < tick) continue;
			if (a[0] - e->amb_lo[s] - 1 < e->amb_hi[s] - e->amb_lo[s]) {   /* lo < id <= hi */
				SSrAmbient_Stop(a[0]);
				e->amb_stopped++;
				break;
			}
		}
	}
	for (u32 i = 0; i < n; i++) {   /* the channels, stopped ones included */
		u32 *a = t + i * 0x16;
		if (a[3]) rb_add_excl(a[3], a[3] + 0x80);
		if (a[4]) rb_add_excl(a[4], a[4] + 0x80);
	}
}

u32 rollback_ambients_stopped(void) { return g_e ? g_e->amb_stopped : 0; }

/* One-shot sounds while ticks run again (0), as usual after (1): lockstep.c's impulse_play then
 * plays only the ones the ticks' first runs didn't. Without it, SS2's impulse players 0x480443
 * (0x480440's driver test) and 0x480560 read Arklite's zero byte in place of the "sound driver"
 * byte *0x5eb757 (0x480443 is lockstep.c's sound seed call when that's in, which the patch then
 * skips). Not 0x47ff50: ambients start their channels there (0x480c20 states 3 and 5), so the
 * ambients and speech lines (taunts) the re-run ticks start played silently. */
static const u8 g_zero_byte;
static const u32 g_impulse_sites[] = { 0x480443, 0x480560 };

void rollback_impulses(int on)
{
	static int muted;
	u8 want[5] = { 0xa0 }, now[5] = { 0xa0 };
	u32 to = on ? 0x5eb757 : (u32)&g_zero_byte, from = on ? (u32)&g_zero_byte : 0x5eb757;
	if (muted == !on) return;
	muted = !on;
	if (lockstep_impulses_mute(!on)) return;
	memcpy(now + 1, &from, 4);
	memcpy(want + 1, &to, 4);
	for (u32 i = 0; i < sizeof g_impulse_sites / sizeof g_impulse_sites[0]; i++)
		if (code_matches(g_impulse_sites[i], now, 5)) patch_write(g_impulse_sites[i], want, 5);
}

/* Put the game back to the start of tick, if it's still kept: 1 done, 0 not kept */
int rollback_restore_tick(u32 tick)
{
	Engine *e = g_e;
	u32 s = tick % RB_SLOTS;
	if (!e || !e->active || !e->slot_valid[s] || e->slot_tick[s] != tick) return 0;
	/* The wall-clock base isn't game state: put back, 0x4fbeb0 would count the rewound ticks as
	 * time that passed and run them once more, so each rollback pushed this PC's clock ahead */
	u32 base = GS_TIME_BASE(ONgGameState);
	rb_unwatch();
	rb_check_allocs();   /* memory another thread freed since this tick began: not put back */
	/* the sound thread waits until the restore is done: it reads memory being put back */
	u8 *lock = SSgLock;
	if (lock) SSrLock(lock);
	rb_sound_before_restore(tick);
	rb_restore(s);
	if (lock) SSrUnlock(lock);
	GS_TIME_BASE(ONgGameState) = base;
	e->last_tick = tick;   /* not "the level restarted" */
	return 1;
}

u32 rollback_known_pages(void) { return g_e && g_e->active ? g_e->npages : 0; }

/* ---- Checking rollbacks in a match (net.c) ------------------------------------------------ *
 * A rollback puts back a tick's start and runs the ticks again with the inputs that came in.
 * Memory it doesn't put back stays as the guessed run left it, and if the ticks read it this
 * PC's game comes out differently from the others' (the 2026-10-29 desync: player 1's z a few
 * float steps off, after the joiner's 210 rollbacks of player 1's input). The rewind test runs
 * ticks again with the same inputs, so it can't see such memory. So some rollbacks are checked
 * in the match: after the re-run, the characters, both RNGs, every known page and the pages
 * dropped as another heap's are kept (rollback_check_begin); net.c puts the same tick back and
 * runs the same ticks with the same inputs once more; rollback_check_end compares. Both runs
 * had the right inputs, so a character that differs is game state a rollback leaves behind.
 * Each check that differs goes to logs\Arklite-rollbackcheck_<date>_<time>.txt: the character
 * fields, then the known pages that differ (first dword, Oni.exe globals pointing near) and the
 * dropped pages that differ. Some page differences are expected: ambient sounds' play ids (a
 * rollback stops the ambients its ticks started; the next run starts them with new ids). A
 * game-state difference with no page listed means memory outside the snapshots (another heap,
 * Daodan, Arklite outside .tstate). */
int rollback_check_begin(void)
{
	Engine *e = g_e;
	if (!e || !e->active) return 0;
	if (!e->vpages) e->vpages = alloc_own(RB_MAX_PAGES * 0x1000);
	if (!e->vdropped) e->vdropped = alloc_own(RB_MAX_DROPPED * 0x1000);
	if (!e->vpages || !e->vdropped) return 0;
	rb_unwatch();
	rb_rd_forget();
	rb_game_capture();
	e->vn = e->npages;
	for (u32 i = 0; i < e->vn; i++)
		if (rb_kept(e->pages[i])) memcpy(e->vpages + i * 0x1000, (const void *)(e->pages[i] << PAGE_SHIFT), 0x1000);
	e->vnd = e->ndropped;
	for (u32 i = 0; i < e->vnd; i++)
		if (rb_readable(e->dropped[i] << PAGE_SHIFT))
			memcpy(e->vdropped + i * 0x1000, (const void *)(e->dropped[i] << PAGE_SHIFT), 0x1000);
	return 1;
}

/* After the second run: 1 if the game state came out the same, 0 if not. what: the rollback */
int rollback_check_end(const char *what)
{
	Engine *e = g_e;
	HANDLE was = g_out;
	u32 game_bad, pages_bad = 0, dropped_bad = 0, shown = 0;
	if (!e || !e->active || !e->vpages) return 1;
	rb_unwatch();
	rb_rd_forget();
	e->vchecks++;
	if (!e->vout || e->vout == INVALID_HANDLE_VALUE) {
		char path[MAX_PATH];
		e->vout = log_file_create("Arklite-rollbackcheck.txt", path);
		g_out = e->vout;
		out("Arklite rollback check: a rollback's ticks run once more with the same inputs; the two runs compared.");
		out("A character that differs is game state a rollback leaves behind. 'first time' = the rollback's run,");
		out("'re-run' = the check's run. Expected page noise: ambient sounds' play ids.");
	}
	g_out = e->vout;
	out("");
	out("check %d: %s", e->vchecks, what);
	game_bad = rb_game_compare();
	for (u32 i = 0; i < e->vn; i++) {
		u32 p = e->pages[i];
		if (!rb_kept(p) || !rb_page_differs(p, e->vpages + i * 0x1000)) continue;
		pages_bad++;
		if (shown++ < 24) rb_out_diff(p, e->vpages + i * 0x1000);
	}
	for (u32 i = 0; i < e->vnd; i++) {
		u32 a = e->dropped[i] << PAGE_SHIFT, k = 0;
		const u32 *now = (const u32 *)a, *want = (const u32 *)(e->vdropped + i * 0x1000);
		if (!rb_readable(a) || !memcmp_pages((const u8 *)now, (const u8 *)want)) continue;
		while (k < 1024 && now[k] == want[k]) k++;
		dropped_bad++;
		if (shown++ < 32)
			out("    DIFFERS 0x%08x (dropped as another heap's), first at +0x%03x: %08x the first time, %08x re-run",
			    a, k * 4, want[k], now[k]);
	}
	if (e->npages > e->vn) out("    %d page(s) first written in the re-run (not compared)", e->npages - e->vn);
	out("  %s; %d known page(s) and %d dropped page(s) differ", game_bad ? "GAME STATE DIFFERS" : "game state matched",
	    pages_bad, dropped_bad);
	g_out = was;
	if (game_bad) {
		e->vbad++;
		if (e->vlogs++ < 8)
			log_msg("rollback: check %d (%s): GAME STATE DIFFERS when the same ticks run twice (%d values; %d known and %d "
			        "dropped pages differ): see Arklite-rollbackcheck in logs", e->vchecks, what, game_bad, pages_bad, dropped_bad);
	}
	return !game_bad;
}

void rollback_check_stats(u32 *checks, u32 *bad)
{
	*checks = g_e ? g_e->vchecks : 0;
	*bad = g_e ? g_e->vbad : 0;
}

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
