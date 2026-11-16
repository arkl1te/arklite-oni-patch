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
static volatile u32 g_vq;   /* VirtualQuery calls, for the phase timing's counts (rollback_phase_text) */
#define VirtualQuery(a, m, n) (g_vq++, VirtualQuery(a, m, n))
__declspec(dllimport) u32    WINAPI GetProcessHeaps(u32, HANDLE *);
__declspec(dllimport) u32    WINAPI GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) HANDLE WINAPI HeapCreate(u32, u32, u32);

#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_TICK(gs)         (*(u32 *)((gs) + 0x144))
#define GS_TIME_BASE(gs)    (*(u32 *)((gs) + 0x140))   /* wall clock (1/60 s) the server time was last moved to */
#define GS_SERVER_TIME(gs)  (*(u32 *)((gs) + 0x148))   /* the update's last tick + 1, while its ticks run (0x4fb11b) */
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

/* A page's hash, 0 if it went away while being read: VirtualQuery's "committed, writable" in
 * hash_all can be out of date by the read, another thread (Oni's heap, the sound thread) can
 * free the page in between. The Windows joiner crashed so in a rollback check as it came into
 * the warm-up (2026-11-13). A fault between rb_hash_lo and rb_hash_hi goes on at
 * rb_hash_fail (rb_probe_fault, below). The same sum as before:
 *   h = ((h ^ p[i]) * 0x01000193 ^ p[i + 1] ^ (p[i + 2] << 7) ^ (p[i + 3] >> 3)) * 0x01000193 */
extern const u8 rb_hash_lo[] __asm__("rb_hash_lo"), rb_hash_hi[] __asm__("rb_hash_hi");
extern const u8 rb_hash_fail[] __asm__("rb_hash_fail");

__attribute__((naked, noinline, used)) static u32 ONI_FC hash_page(const u32 *p)
{
	__asm__ volatile(
		"pushl %esi\n\t"
		"movl %ecx, %esi\n\t"
		"movl $0x811c9dc5, %eax\n\t"
		"xorl %ecx, %ecx\n"
		"rb_hash_lo:\n\t"
		"xorl (%esi,%ecx,4), %eax\n\t"
		"imull $0x01000193, %eax, %eax\n\t"
		"xorl 4(%esi,%ecx,4), %eax\n\t"
		"movl 8(%esi,%ecx,4), %edx\n\t"
		"shll $7, %edx\n\t"
		"xorl %edx, %eax\n\t"
		"movl 12(%esi,%ecx,4), %edx\n\t"
		"shrl $3, %edx\n\t"
		"xorl %edx, %eax\n\t"
		"imull $0x01000193, %eax, %eax\n\t"
		"addl $4, %ecx\n\t"
		"cmpl $1024, %ecx\n\t"
		"jb rb_hash_lo\n"
		"rb_hash_hi:\n\t"
		"popl %esi\n\t"
		"ret\n"
		"rb_hash_fail:\n\t"
		"xorl %eax, %eax\n\t"
		"popl %esi\n\t"
		"ret\n");
}

/* Hash every page of the listed regions still writable now into out[], 0 for gone ones */
static void hash_regions(u32 *out, const Region *regs, u32 nregs)
{
	MEMORY_BASIC_INFORMATION m;
	for (u32 i = 0; i < nregs; i++) {
		const Region *r = &regs[i];
		for (u32 a = r->base; a < r->base + r->size; ) {
			if (VirtualQuery((void *)a, &m, sizeof m) != sizeof m) break;
			u32 end = (u32)m.BaseAddress + m.RegionSize;
			if (end > r->base + r->size) end = r->base + r->size;
			for (; a < end; a += 0x1000)
				out[a >> PAGE_SHIFT] = writable(&m) ? hash_page((const u32 *)a) : 0;
		}
	}
}

static void hash_all(u32 *out) { hash_regions(out, g_regions, g_nregions); }

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
 * frame (RB_WATCH_FIRST frames, then a tenth of them each frame, so each one every RB_WATCH_EVERY
 * frames: all at once every 10th tick was ~700 system calls in one frame, and with rollbacks those
 * ticks were mostly re-run ones, watching only the microseconds to the next tick), between the
 * frame's last tick and its next one, the known pages are made PAGE_NOACCESS until the next tick: the main thread (drawing) touching one only
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
/* Oni's table of the positioned ambients it plays (Oni_Sound2.c OSgPlayingAmbient): 1024 handles
 * at *0x61fa34, each a 0x4c-byte entry (+0 index, +4 the handle a particle or object holds, +8
 * the ambient, +0xc SS2's play id) or null. Game memory: a rollback puts it back. */
#define OSgPlayingHandles    (*(u8 ***)0x61fa34)
#define OScPlayingHandles    1024
#define OS_PLAY_ID(a)        (*(const u32 *)((a) + 0xc))
#define SSrAmbient_Start     0x4821f0
#define SS_STATE_STOPPING    7
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
#define PF_GONE          64               /* known, but its last snapshot copy faulted (decommitted): asked about until it's back */
#define RB_MAX_EXCL      1024             /* ranges never put back (SS2's sound state, open ACM streams) */
#define RB_MAX_NOTES     48               /* pages that became shared, logged from the main thread */
#define RB_WATCH_EVERY   10
#define RB_MAX_DROPPED   1024             /* pages dropped as another heap's, kept for rollback checks */
#define RB_WATCH_FIRST   120
#define RB_MAX_REPROT    1024             /* pages opened between two ticks of one update before a full protect is due */

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
	u32 frames_ended;              /* updates whose last tick handed the memory back to the frame (rb_watch) */
	u32 fault_page, fault_tid, fault_repeat, forced;   /* a fault that keeps coming back */
	volatile u32 body_serial, in_fault;   /* rb_watchdog: ticks begun; the main thread is in rb_fault */
	volatile long learn_lock;      /* rb_learn from the main thread, another thread and the watchdog */
	u16 order[RB_MAX_PAGES];       /* pages[] indexes by address, the first order_n of them (rb_order) */
	u32 order_n;
	u8 *omask;                     /* by place in pages[]: 128 bytes, the dwords seen changing between ticks */
	u8 *tmask;                     /* by place in pages[]: 128 bytes, the dwords seen changing in a tick */
	u8 *endc;                      /* by place in pages[]: shared pages as the last tick ended */
	u8 endc_have[RB_MAX_PAGES];    /* endc holds this page (it was shared when the last tick ended) */
	u32 endc_ok;                   /* endc holds the last tick's end (no restore since) */
	u32 amb_lo[RB_SLOTS], amb_hi[RB_SLOTS];   /* SS2 ambient play ids the slot's tick started: (lo, hi] */
	u32 ex_lo[RB_MAX_EXCL], ex_hi[RB_MAX_EXCL], nex;
	u32 os_learned;                /* pages of Oni's ambient table made known by rb_os_table_known */
	u32 note_page[RB_MAX_NOTES], note_tid[RB_MAX_NOTES], nnotes, notes_logged;
	u32 amb_stopped;
	u32 acm_hits, acm_logs;        /* rb_acm_guard: ACM memory found in known pages */
	u32 probe_faults;              /* rb_read8: reads of memory gone after rb_readable said yes */
	u32 grow, grown, grow_logs;    /* rb_grow: Oni took memory the snapshots don't cover; pages added; logged */
	u32 freed, freed_logs;         /* rb_forget: pages of allocations Oni gave back */
	u32 reused, heap_pages, heap_logs, heap_walked;   /* rb_check_allocs, rb_heaps_guard */
	u32 file_logs;                 /* rb_file_blocks_out */
	/* the protection kept between the ticks of one update ("Protection between ticks") */
	volatile u32 gap;              /* between two ticks of one update, the last tick's protection still on */
	u32 layout, gap_layout;        /* candidates changed (forgotten, dropped, taken): the kept runs don't fit any more */
	u32 reprot[RB_MAX_REPROT], nreprot, reprot_over;   /* pages opened in the gap, protected again for the next tick */
	u32 gap_ticks, gap_faults, gap_late;               /* (rollback_phase_text) */
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
static char g_vpath[MAX_PATH];   /* the rollback check file (rollback_check_path) */

static void rb_unwatch(void);
static void rb_file_blocks_out(void);
static void rb_layout_change(void);
static void rb_gap_open_locked(u32 p);
static void rb_unprotect(void);
static int rb_kept(u32 p);
static void rb_globals_near(u32 p, char *out_buf);
static void rb_sound_before_restore(u32 tick);
static void rb_os_table_known(void);
static void rb_os_handles_repair(u32 tick);
static u32 e_os_repairs;   /* restores after which Oni's ambient handles were made again (rb_os_handles_repair) */

static u32 g_rs_pages;   /* (rb_restore counts the pages it puts back: rollback_phase_text) */

/* Still committed and writable (a tick may have freed it). One VirtualQuery a page was most of a
 * tick's snapshot copy (2026-11-15: 350-900 calls a tick, ~0.5 ms of it under Wine): each "yes"
 * covers the rest of the run of pages it came from (Windows answers from the page asked about to
 * the run's end, so passes go in address order: rb_order), until pw_forget, which each pass over
 * the pages calls first (a pass runs no game code; protections and the heap change between passes) */
#define PW_CACHE 8
static u32 g_pw_lo[PW_CACHE], g_pw_hi[PW_CACHE], g_pw_next;

static void pw_forget(void)
{
	memset(g_pw_lo, 0, sizeof g_pw_lo);
	memset(g_pw_hi, 0, sizeof g_pw_hi);
}

static int page_writable(u32 p)
{
	MEMORY_BASIC_INFORMATION m;
	u32 a = p << PAGE_SHIFT;
	for (u32 i = 0; i < PW_CACHE; i++)
		if (a >= g_pw_lo[i] && a < g_pw_hi[i]) return 1;
	if (VirtualQuery((const void *)a, &m, sizeof m) != sizeof m || !writable(&m)) return 0;
	g_pw_lo[g_pw_next] = (u32)m.BaseAddress;
	g_pw_hi[g_pw_next] = (u32)m.BaseAddress + m.RegionSize;
	g_pw_next = (g_pw_next + 1) % PW_CACHE;
	return 1;
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

/* Reads the 8 bytes at a into out[0], out[1]: 1, or 0 when either read faulted (rb_probe_fault
 * sends a fault between rb_probe_lo and rb_probe_hi to rb_probe_fail). rb_readable's answer can
 * be out of date by the time the memory is read: another thread can free it in between (the
 * cache keeps an answer for a whole restore). A Windows host crashed so in rb_lock_mask
 * (v0.4.0, 2026-11-12): a float, 20.0 (0x41a00000), looked like a critical
 * section's DebugInfo, passed rb_readable, and reading it faulted. */
extern const u8 rb_probe_lo[] __asm__("rb_probe_lo"), rb_probe_hi[] __asm__("rb_probe_hi");
extern const u8 rb_probe_fail[] __asm__("rb_probe_fail");

__attribute__((naked, noinline, used)) static int ONI_FC rb_read8(u32 a, u32 *out)
{
	__asm__ volatile(
		"rb_probe_lo:\n\t"
		"movl (%ecx), %eax\n\t"
		"movl %eax, (%edx)\n\t"
		"movl 4(%ecx), %eax\n\t"
		"rb_probe_hi:\n\t"
		"movl %eax, 4(%edx)\n\t"
		"movl $1, %eax\n\t"
		"ret\n"
		"rb_probe_fail:\n\t"
		"xorl %eax, %eax\n\t"
		"ret\n");
}

/* Copies a 4 KB page: 1, or 0 when reading src faulted (rb_probe_fault sends a fault between rb_copy_lo and
 * rb_copy_hi to rb_copy_fail). The snapshot copies every known page each tick without asking the system
 * first whether a tick freed it: that question was one VirtualQuery per run of pages, ~57 calls a tick,
 * 2.5-9 us each on Windows (2026-11-16). A page that faults is PF_GONE and asked about until it is back. */
extern const u8 rb_copy_lo[] __asm__("rb_copy_lo"), rb_copy_hi[] __asm__("rb_copy_hi");
extern const u8 rb_copy_fail[] __asm__("rb_copy_fail");

__attribute__((naked, noinline, used)) static int ONI_FC rb_copy_page(void *dst, const void *src)
{
	__asm__ volatile(
		"pushl %esi\n\t"
		"pushl %edi\n\t"
		"movl %ecx, %edi\n\t"
		"movl %edx, %esi\n\t"
		"movl $1024, %ecx\n\t"
		"cld\n"
		"rb_copy_lo:\n\t"
		"rep movsl\n"
		"rb_copy_hi:\n\t"
		"popl %edi\n\t"
		"popl %esi\n\t"
		"movl $1, %eax\n\t"
		"ret\n"
		"rb_copy_fail:\n\t"
		"popl %edi\n\t"
		"popl %esi\n\t"
		"xorl %eax, %eax\n\t"
		"ret\n");
}

/* Asked last, when the page watch didn't take the fault: a known page shut between ticks is
 * opened and read as before, only memory that is really gone makes rb_read8 answer 0 */
static long rb_probe_fault(EXCEPTION_POINTERS *ep)
{
	u32 code = ep->record->code, *eip = (u32 *)((u8 *)ep->context + 0xb8);   /* i386 CONTEXT Eip */
	if (code != 0xc0000005u && code != 0xc0000006u && code != 0x80000001u /* guard page */) return 0;
	if (*eip >= (u32)rb_probe_lo && *eip < (u32)rb_probe_hi) *eip = (u32)rb_probe_fail;
	else if (*eip >= (u32)rb_hash_lo && *eip < (u32)rb_hash_hi) *eip = (u32)rb_hash_fail;   /* hash_page */
	else if (*eip >= (u32)rb_copy_lo && *eip < (u32)rb_copy_hi) *eip = (u32)rb_copy_fail;   /* rb_copy_page */
	else return 0;
	if (g_e) {
		g_e->probe_faults++;
		g_e->fault_page = 0;   /* (moved on: not a fault that keeps coming back) */
	}
	return -1;   /* EXCEPTION_CONTINUE_EXECUTION, at rb_probe_fail */
}

static long WINAPI rb_fault(EXCEPTION_POINTERS *ep)
{
	Engine *e = g_e;
	u32 tid = GetCurrentThreadId();
	long r;
	/* the match's snapshots are off (rb_reset opened every page): any fault now is someone
	 * else's. Handled, a real crash during the next level's load looped here forever (the Windows
	 * joiner froze going back to the lobby after a match) */
	if (!e || !e->active) return rb_probe_fault(ep);
	if (tid != e->main_tid) {
		r = rb_fault_main(ep, tid);
	} else {
		e->in_fault = 1;
		r = rb_fault_main(ep, tid);
		e->in_fault = 0;
	}
	return r ? r : rb_probe_fault(ep);
}

static long rb_fault_main(EXCEPTION_POINTERS *ep, u32 tid)
{
	EXCEPTION_RECORD *r = ep->record;
	Engine *e = g_e;
	u32 p, old;

	if (r->code != 0xc0000005u || r->nparams < 2) return 0;
	p = r->info[1] >> PAGE_SHIFT;
	if (p >= PAGES || !(e->flag[p] & PF_CAND)) return 0;
	if (e->gap && r->info[0] == 1 && (e->flag[p] & PF_PROT)) {   /* between two ticks of one update: opened, nothing learned */
		rb_lock();
		if (e->gap && (e->flag[p] & PF_PROT)) rb_gap_open_locked(p);
		rb_unlock();
		if (!(e->flag[p] & PF_PROT)) return -1;
	}
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
	rb_file_blocks_out();
	/* other threads: two hashes back to back. A page that changed between them is another
	 * thread's too (the sound thread mixing: the match's start sound plays right now), but ticks
	 * may write game state into it as well (Oni's heap mixes them). Once marked foreign, never
	 * copied or put back: the Linux host of 2026-11-06 started a match with 26 such pages and
	 * came apart at tick 7140 (the host's own player 0.386 units off on the joiner); its other
	 * sessions started with 0 or 1 and stayed in sync. So they're known and shared from the
	 * start, as a page another thread touches later is: put back only in the dwords ticks
	 * alone change (tmask, omask). Known pages are never protected, so no new faults.
	 * Only the candidate regions are hashed (the loop below skips every other page): hashing all
	 * of the process's writable memory twice froze a joiner's screen for 1.4 s as it came into
	 * the game, every join (Windows and Wine logs, 2026-11-13). */
	u32 hash_t0 = GetTickCount();
	hash_regions(g_h0, e->regs, e->nregs);
	hash_regions(g_h1, e->regs, e->nregs);
	hash_t0 = GetTickCount() - hash_t0;
	e->npages = e->overflow = e->order_n = 0;
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
	e->os_learned = 0;
	rb_os_table_known();   /* known from the start (see "The ambient table as one") */
	if (e->os_learned) log_msg("rollback: Oni's ambient table (its array, entries and handles) is known from the start: %u page(s)", e->os_learned);
	e->endc_ok = 0;
	e->nnotes = e->notes_logged = 0;
	e->amb_stopped = e->acm_hits = e->acm_logs = e->probe_faults = 0;
	e->grow = e->grown = e->grow_logs = e->plist_logs = e->plist_cuts = e->freed = e->freed_logs = 0;
	e->reused = e->heap_pages = e->heap_logs = e->heap_walked = e->ndropped = e->seg_kept = 0;
	e->vchecks = e->vbad = e->vlogs = 0;
	e->frames_ended = 0;
	e->main_tid = GetCurrentThreadId();
	if (!e->main_thread) e->main_thread = OpenThread(0x1fffff /* THREAD_ALL_ACCESS */, 0, e->main_tid);
	__asm__ volatile("movl %%fs:4, %0" : "=r"(e->stack_top));
	e->active = 1;
	u32 pages = 0;
	for (u32 i = 0; i < e->nregs; i++) pages += e->regs[i].size >> PAGE_SHIFT;
	log_msg("rollback: snapshots on: %d allocations of Oni's runtime, %d candidate regions (%d KB), %d pages changed by "
	        "other threads at the start (hashed in %u ms)", e->noni, e->nregs, pages * 4, e->nforeign_pages, hash_t0);
}

static void rb_reset(const char *why)
{
	if (!g_e->active) return;
	rb_unwatch();
	if (g_e->nruns) rb_unprotect();
	g_e->gap = g_e->nreprot = g_e->reprot_over = 0;
	log_msg("rollback: snapshots off (%s); %d pages were known, %d KB taken during the match followed, %d KB given back "
	        "dropped, %d broken particle lists cut, ACM memory in known pages %d times, %d freed addresses taken again, %d KB of "
	        "other heaps dropped, %d reads of memory freed under the lock test caught", why, g_e->npages, g_e->grown * 4,
	        g_e->freed * 4, g_e->plist_cuts, g_e->acm_hits, g_e->reused, g_e->heap_pages * 4, g_e->probe_faults);
	if (g_pool) log_msg("rollback: the pool for Oni's big blocks: at most %d of %d slots in use at once this match", g_pool_used_max, POOL_SLOTS);
	if (e_os_repairs) log_msg("rollback: Oni's ambient handles were made again from its array after %u restores this match", e_os_repairs);
	e_os_repairs = 0;
	g_pool_used_max = 0;
	g_e->active = 0;
	if (g_e->vout && g_e->vout != INVALID_HANDLE_VALUE) CloseHandle(g_e->vout);
	g_e->vout = 0;
}

/* The known pages' indexes in address order (pages[] has them in the order they were learned).
 * A page's writability is a system call (page_writable), and Windows answers it from the page
 * asked about to the end of its run, not from the run's start: taken in the learned order, a
 * lower page of a run already asked about was asked again, about 50 calls of 4-8 us each per
 * pass on a Windows laptop. In address order one answer covers the rest of its run. pages[] only
 * grows (rb_learn) until the engine starts again (order_n back to 0 with npages), and doesn't
 * change while the main thread is between ticks or before protecting: new indexes go in by
 * insertion. */
static const u16 *rb_order(void)
{
	Engine *e = g_e;
	while (e->order_n < e->npages) {
		u32 i = e->order_n++, j = i, p = e->pages[i];
		while (j && e->pages[e->order[j - 1]] > p) {
			e->order[j] = e->order[j - 1];
			j--;
		}
		e->order[j] = (u16)i;
	}
	return e->order;
}

static void rb_snapshot(u32 tick, const void *action)
{
	Engine *e = g_e;
	u32 s = tick % RB_SLOTS;

	pw_forget();
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
	const u16 *order = rb_order();   /* (by address: page_writable asks once per run) */
	for (u32 o = 0; o < e->npages; o++) {
		u32 i = order[o], p = e->pages[i];
		u8 *f = &e->flag[p];
		if (!(*f & PF_CAND)) continue;
		if (*f & PF_GONE) {
			if (!page_writable(p)) continue;
			*f &= ~PF_GONE;
		}
		if (!rb_copy_page(slot_page(s, i), (const void *)(p << PAGE_SHIFT))) *f |= PF_GONE;
	}
	e->slot_tick[s] = tick;
	e->slot_valid[s] = 1;
	e->has_action[s] = action != 0;
	if (action) memcpy(e->action[s], action, 0x30);
	e->cur = s;
}

/* Readable now. Asked of the system, not of the region list made at the engine's start: memory
 * freed since then (a heap shrinking) made a restore read a decommitted page and crash. The last
 * few readable regions are cached for one restore or comparison (rb_rd_forget clears them:
 * the game's own memory isn't freed while the main thread is in there). Other threads (sound,
 * drivers) can free theirs at any time, so a "yes" can be out of date when the memory is read:
 * memory that isn't the game's is read through rb_read8, which can't crash. */
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

/* The lock scan's "not readable" answers, for one pass (rb_rd_forget clears them with the readable
 * ones): float values that pass the shape test point into unused address space, which wasn't
 * remembered, so each restore asked again for each of them (2026-11-15, Windows: 40 us a page put
 * back, 2.6 ms a rollback). Only the lock scan uses these (lm_readable): elsewhere a "no" must be
 * asked again. */
#define LM_NEG 32
static u32 g_lm_lo[LM_NEG], g_lm_hi[LM_NEG], g_lm_next;

static void rb_rd_forget(void)
{
	memset(g_rd_hi, 0, sizeof g_rd_hi);
	memset(g_lm_lo, 0, sizeof g_lm_lo);
	memset(g_lm_hi, 0, sizeof g_lm_hi);
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

/* Readable, for the lock scan: rb_readable's cache, then the scan's own "no"s (above) */
static int lm_readable(u32 a)
{
	MEMORY_BASIC_INFORMATION m;
	for (u32 i = 0; i < RB_RD_CACHE; i++)
		if (a >= g_rd_lo[i] && a < g_rd_hi[i]) return 1;
	for (u32 i = 0; i < LM_NEG; i++)
		if (a >= g_lm_lo[i] && a < g_lm_hi[i]) return 0;
	if (rb_readable(a)) return 1;
	if (VirtualQuery((const void *)a, &m, sizeof m) == sizeof m && m.RegionSize) {
		g_lm_lo[g_lm_next] = (u32)m.BaseAddress;
		g_lm_hi[g_lm_next] = (u32)m.BaseAddress + m.RegionSize;
		g_lm_next = (g_lm_next + 1) % LM_NEG;
	}
	return 0;
}

/* The 8 bytes at d lie on candidate pages (Oni's own memory: never guard pages) that are neither
 * watched nor protected, so rb_read8 can read them without asking the system first: a page
 * decommitted since faults, and rb_read8 answers 0 as rb_readable would have. Most pointer-like
 * words the lock scan follows point into Oni's memory, and asking the system about each cost
 * half of a restore's VirtualQuery calls (Windows laptop, 2026-11-15). */
static int rb_own_pair(u32 d)
{
	const u8 *f = g_e->flag;
	return (f[d >> PAGE_SHIFT] & (PF_CAND | PF_WATCH | PF_PROT)) == PF_CAND &&
	       (f[(d + 7) >> PAGE_SHIFT] & (PF_CAND | PF_WATCH | PF_PROT)) == PF_CAND;
}

/* g_e->mask[k] = 1 for the dwords of the page that belong to a live critical section: its
 * DebugInfo (+0) points to a debug record whose CriticalSection field (+4) points back. Only where
 * the page and other (the copy it is put back from or compared with) differ: a dword the same in
 * both comes out the same masked or not, so a section none of whose 24 bytes differ isn't tested.
 * That test is a system call for most pointer-like words (the debug records are outside Oni's
 * memory), and testing every word of every page put back cost a Windows restore ~2 ms, ~200 calls
 * (2026-11-16; Windows finds ~14 sections a restore, Wine none: its sections have no debug record). */
static void rb_lock_mask(u32 p, const u32 *other)
{
	const u32 *page = (const u32 *)(p << PAGE_SHIFT);
	u32 next = 1024;   /* the first dword at or after k where page and other differ */
	memset(g_e->mask, 0, sizeof g_e->mask);
	for (u32 k = 1024; k-- > 0; ) {
		u32 d = page[k];
		if (d != other[k]) next = k;
		if (next >= k + 6) continue;   /* (a section is 6 dwords: none of them differs) */
		if ((d & 3) || d < 0x10000 || k + 3 >= 1024) continue;
		/* it must look like one: a free/held LockCount, a small RecursionCount, the owner a
		 * thread id or 0. A two-way list node ({next, prev}) passes the pointer test below, so
		 * 24 bytes of every such node were never put back and the lists came apart. Tested
		 * first, from the page alone: the pointer test is a system call, and floats that look
		 * like pointers made a restore cost tens of thousands of them (a visible hitch) */
		int lock = (int)page[k + 1];
		if (lock < -0x10000 || lock > 0x10000 || page[k + 2] > 0x10000 || page[k + 3] > 0x100000) continue;
		u32 rec[2];   /* the debug record: +0 Type (u16), +4 CriticalSection */
		if (!rb_own_pair(d) && (!lm_readable(d) || !lm_readable(d + 7))) continue;
		if (!rb_read8(d, rec)) continue;
		if (rec[1] != (u32)(page + k)) continue;
		if ((u16)rec[0] != 0) continue;   /* debug record Type: RTL_CRITSECT_TYPE */
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
	rb_lock_mask(p, src);
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
	pw_forget();
	/* a page as it was needs nothing put back: most known pages don't change from one tick to the
	 * next, and the per-page work (live locks, masks) on all of them made a rollback cost 14 ms
	 * late in a match on the Windows joiner, one per tick while the host turned the camera */
	const u16 *order = rb_order();
	for (u32 o = 0; o < e->npages; o++) {
		u32 i = order[o];
		if (rb_kept(e->pages[i]) && memcmp_pages((const u8 *)(e->pages[i] << PAGE_SHIFT), slot_page(slot, i))) {
			rb_restore_page(i, slot_page(slot, i));
			g_rs_pages++;
		}
	}
	e->endc_ok = 0;
}

/* The page differs from a copy, live critical sections left out */
static int rb_page_differs(u32 p, const u8 *want)
{
	const u32 *now = (const u32 *)(p << PAGE_SHIFT), *w = (const u32 *)want;
	if (!memcmp_pages((const u8 *)now, want)) return 0;
	rb_rd_forget();
	rb_lock_mask(p, w);
	for (u32 k = 0; k < 1024; k++)
		if (!g_e->mask[k] && now[k] != w[k]) return 1;
	return 0;
}

/* ---- Protection between ticks ------------------------------------------------------------ *
 * Protecting the unknown candidates before a tick and opening them after it is two system calls
 * per run of pages, 60-120 runs: 0.6 ms a tick on Windows, 1-1.5 ms under Wine, against 0.1 ms for
 * the tick itself (2026-11-15). A rollback runs its ticks again 6 at a time in one update
 * (net.c rb_resimulate, Oni's loop 0x4fb12c caps an update at 6), and between two of them only
 * the tick hook's ends, the camera (0x4fb141) and the tick count run. So while more ticks follow
 * in the same update (GS_TICK + 1 < GS_SERVER_TIME, which Oni sets to the last tick + 1 before
 * the loop), the protection stays on (gap): a write to a protected page then, by any thread,
 * opens it as if nothing were protected (rb_gap_open_locked), and the next tick protects the
 * pages so opened again (rb_gap_reprotect) instead of all of them. Nothing is learned in a gap,
 * as nothing was between ticks before. A full protect instead when the candidates changed since
 * (layout: rb_forget, rb_grow, a drop) or too many pages were opened; the update's last tick
 * opens everything as before, and the frame's camera call after the loop (0x4fb170, net.c)
 * ends a gap still on (rollback_update_end: never expected). */
static int g_gap_allowed;   /* net.c hooks the frame's camera call (rollback_gap_allow) */

void rollback_gap_allow(void) { g_gap_allowed = 1; }

/* A rollback's re-run and the frame's own ticks after it share one protection (net.c holds it, rollback_gap_hold):
 * between them only Arklite's network code and the end of Oni's update (camera, particles' frame step, the sound
 * listener) run, and a write there faults and opens its page as in any gap. Checked 2026-11-16 (a TEMP build that
 * left one held rollback in 50 open and compared every unknown candidate across the window): every page that changed
 * there faulted in the held windows too, on Wine and Windows (~40000 held windows, 0 missed). The drawing that
 * follows does write from system calls (OpenGL under Wine): the frame's own update, or net_update's end, opens it. */
static int g_hold;           /* net.c: a re-run is running whose last tick keeps the protection */
static u32 g_held;           /* the protection is kept past an update's end by a hold, until the next tick */

/* net.c, around a rollback's re-run (and its check's): its last tick keeps the protection for the frame's own
 * ticks. Off again before those run; net_update's end closes a held protection no tick took over
 * (rollback_gap_close). */
void rollback_gap_hold(int on) { g_hold = on; }
void rollback_gap_close(void);

/* A protected page written between two ticks of one update: open, and noted (under learn_lock) */
static void rb_gap_open_locked(u32 p)
{
	Engine *e = g_e;
	u32 old;
	if (!VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x40 : 0x04, &old)) return;
	if (e->nreprot < RB_MAX_REPROT) e->reprot[e->nreprot++] = p;
	else e->reprot_over = 1;
	e->flag[p] &= ~PF_PROT;
	e->gap_faults++;
}

/* The next tick of the same update: what the gap opened, protected again. A page learned since
 * (rb_learn_range) stays open, as known pages are. Ends the gap under learn_lock, so a fault on
 * another thread either opened its page before (and it's in the list) or finds the gap over. */
static void rb_gap_reprotect(void)
{
	Engine *e = g_e;
	u32 old;
	rb_lock();
	for (u32 i = 0; i < e->nreprot; i++) {
		u32 p = e->reprot[i];
		if (!(e->flag[p] & PF_CAND) || e->index[p] || (e->flag[p] & (PF_FOREIGN | PF_PROT))) continue;
		if (VirtualProtect((void *)(p << PAGE_SHIFT), 0x1000, e->flag[p] & PF_EXEC ? 0x20 : 0x02, &old)) e->flag[p] |= PF_PROT;
	}
	e->nreprot = 0;
	e->gap = 0;
	rb_unlock();
}

static void rb_unprotect(void);

/* The kept protection off, as at the end of an update's last tick */
static void rb_gap_end(void)
{
	Engine *e = g_e;
	if (!e->gap) return;
	rb_unprotect();
	rb_lock();
	e->nreprot = e->reprot_over = 0;
	e->gap = 0;
	rb_unlock();
}

/* The candidates are about to change (forgotten, dropped, taken): a kept protection ends first, so
 * no page leaves the candidates still protected (a fault there would be no one's), and the tick
 * that changed them doesn't keep its protection either */
static void rb_layout_change(void)
{
	rb_gap_end();
	g_e->layout++;
}

/* net.c: the frame's camera call, right after Oni's tick loop */
void rollback_update_end(void)
{
	Engine *e = g_e;
	if (!e || !e->gap) return;
	if (g_hold) return;   /* a re-run's update: the frame's own ticks take the protection over */
	if (g_held) {   /* held for this frame's ticks, and the frame runs none */
		rollback_gap_close();
		return;
	}
	if (e->gap_late++ < 4)
		log_msg("rollback: tick %d: the update ended with the protection kept for a next tick; opened now", e->last_tick);
	rb_gap_end();
}

/* net.c, at net_update's end: a protection a re-run kept that no tick took over (the frame ran no tick of its
 * own) is opened before drawing. Normally the frame's own update ended it already. */
void rollback_gap_close(void)
{
	Engine *e = g_e;
	g_held = 0;
	if (e && e->active && e->gap) rb_gap_end();
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

/* The known pages i with i % RB_WATCH_EVERY == part, or all of them (part < 0) */
static void rb_watch(int part)
{
	Engine *e = g_e;
	u32 old;
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		if (part >= 0 && i % RB_WATCH_EVERY != (u32)part) continue;
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

/* ---- Oni's memory between ticks ------------------------------------------------------------ *
 * Everything Oni allocates comes from its C runtime (VC6): blocks up to *0x55c014 (1016) bytes
 * from its small-block heap (__sbh_alloc_block 0x524196, in 1 MB regions of its own), bigger
 * ones from the heap *0x6d9200. Both are game memory: a rollback puts their pages back as they
 * were at a tick's start, the allocators' own free lists included (the small-block heap keeps
 * them inside the pages it hands out). That's right for what ticks allocate, but between ticks
 * the main thread allocates too: every dialog and window (WMrWindow_New 0x475160 through
 * 0x424ab0), so the pause menu, its sub-dialogs and the F1 Scoreboard. A rollback to a tick from
 * before such a window put its page back over it: its memory held older bytes (freed game data)
 * and the allocator took it for free again, so the next ticks put game data where the window
 * lived. The window manager then read floats and text as pointers (the Linux host's crashes of
 * 2026-11-10, at 0x46db31 and 0x476298 under the pause menu), and its writes changed game data
 * on that PC alone.
 * So while snapshots are on, the main thread's allocations between ticks come from a heap of
 * Arklite's (g_side), which nothing ever puts back: the small-block heap declines them (its
 * three callers, malloc, realloc and calloc, then ask HeapAlloc, as when it's full), its in-place
 * resize too (realloc then moves the block), and rb_heap_alloc / rb_heap_realloc serve them from
 * g_side. A survey of a match with bots opening the pause menu found only the window manager
 * allocating there. Free, realloc and _msize find such a block by its address (g_side_set) at
 * any time, after the match too. One freed inside a tick isn't freed: a rollback can run that
 * tick again (and free it again), or not free it at all; it stays as a leak (logged). */
#define SBH_ALLOC_FN        0x524196   /* __sbh_alloc_block(size): 0 when it can't */
#define SBH_RESIZE_FN       0x52464b   /* __sbh_resize_block(header, block, size): 0 when it can't */
#define SBH_RESIZE_CALL     0x51fcf0   /* realloc's */
static const u32 g_sbh_alloc_calls[] = { 0x51fc70 /* malloc */, 0x51fd01 /* realloc */, 0x523d21 /* calloc */ };
#define SIDE_BITS           15
#define SIDE_SLOTS          (1u << SIDE_BITS)   /* open addressing: 0 empty, 1 deleted */
static HANDLE g_side;
static u32 g_side_set[SIDE_SLOTS];
static u32 g_side_live, g_side_used, g_side_most, g_side_kept, g_side_logs;

static u32 side_hash(u32 p) { return ((p >> 3) * 2654435761u) >> (32 - SIDE_BITS); }

static u32 *side_find(u32 p)
{
	for (u32 i = side_hash(p), n = 0; n < SIDE_SLOTS; i = (i + 1) & (SIDE_SLOTS - 1), n++) {
		if (g_side_set[i] == p) return &g_side_set[i];
		if (!g_side_set[i]) return 0;
	}
	return 0;
}

static int side_has(const void *p) { return g_side && p && side_find((u32)p) != 0; }

static int side_add(void *p)
{
	if (g_side_used >= SIDE_SLOTS / 2) return 0;   /* (deleted slots count: the set stays fast) */
	for (u32 i = side_hash((u32)p); ; i = (i + 1) & (SIDE_SLOTS - 1))
		if (g_side_set[i] <= 1) {
			if (!g_side_set[i]) g_side_used++;
			g_side_set[i] = (u32)p;
			if (++g_side_live > g_side_most) g_side_most = g_side_live;
			return 1;
		}
}

static void side_remove(const void *p)
{
	u32 *e = side_find((u32)p);
	if (e) {
		*e = 1;
		g_side_live--;
	}
}

/* The main thread between ticks while snapshots are on */
static int between_ticks(void)
{
	Engine *e = g_e;
	return g_side && e && e->active && !e->in_body && GetCurrentThreadId() == e->main_tid;
}

static void *side_alloc(u32 flags, u32 n)
{
	void *p = g_heap_alloc(g_side, flags & HEAP_ZERO_MEMORY, n ? n : 1);
	if (p && !side_add(p)) {   /* the set is full: Oni's heap after all, as before */
		g_heap_free(g_side, 0, p);
		if (g_side_logs++ < 4) log_msg("rollback: %d blocks made between ticks are alive: the next one comes from Oni's heap", g_side_live);
		return 0;
	}
	return p;
}

/* (through heap_check.c's journal, which just passes them on when it's off) */
static void *__cdecl rb_sbh_alloc(u32 n)
{
	return between_ticks() ? 0 : heap_sbh_alloc(n);
}

static int __cdecl rb_sbh_resize(void *header, void *block, u32 n)
{
	return between_ticks() ? 0 : heap_sbh_resize(header, block, n);
}

/* The call at a calls fn itself or heap_check.c's wrapper of it: that target, else 0 */
static u32 sbh_call(u32 a, u32 fn, u32 wrapper)
{
	u32 to;
	if (!code_matches(a, (const u8[]){ 0xe8 }, 1)) return 0;
	to = a + 5 + *(const u32 *)(a + 1);
	return to == fn || to == wrapper ? to : 0;
}

static void rb_side_install(void)
{
	u32 ok = 0, to[4];
	for (u32 i = 0; i < 3; i++)
		if ((to[i] = sbh_call(g_sbh_alloc_calls[i], SBH_ALLOC_FN, (u32)heap_sbh_alloc))) ok++;
	if ((to[3] = sbh_call(SBH_RESIZE_CALL, SBH_RESIZE_FN, (u32)heap_sbh_resize))) ok++;
	if (ok != 4 || *(const HANDLE *)0x6d9200 == 0 || !(g_side = HeapCreate(0, 0, 0))) {
		g_side = 0;
		log_msg("rollback: Oni's allocator isn't as expected (%d of 4 calls): windows opened in a match stay in game memory", ok);
		return;
	}
	for (u32 i = 0; i < 3; i++) patch_call(g_sbh_alloc_calls[i], to[i], (u32)rb_sbh_alloc);
	patch_call(SBH_RESIZE_CALL, to[3], (u32)rb_sbh_resize);
	log_msg("rollback: what Oni allocates between ticks (windows, dialogs) comes from Arklite's heap: a rollback never puts it back");
}

/* ---- Body-surface caches outside the snapshots ---------------------------------------------- *
 * A character class's body-surface cache (0x4f2550: 0x3a0 bytes from UUrMemory_Block_New
 * 0x424ab0, its pointer at ONCC +0xc30) is made the first time a particle of one of the class's
 * characters emits from the body: the Daodan glow (superglow_e01, murosuperglow_e01: emitter
 * position BodyBones, P3iEmitParticle 0x45de78) or Mutant Muro's (muroglow_e01, w14_muro_e01:
 * BodySurface, 0x45ddda). Konoko's and Muro's classes have the glow; with the Daodan mutator every
 * class does. The class's dispose (0x4f2520) frees it: the level's unload, level0's at a level0
 * swap, or Oni closing.
 * On Windows Oni maps its level files copy-on-write (MapViewOfFile with FILE_MAP_COPY, 0x402485):
 * the class, and +0xc30 with it, is in no snapshot (rb_candidate leaves mapped memory out; the
 * snapshots cover ~47 MB there against ~330 MB under Wine, whose views are private memory), while
 * the cache's block is in the small-block heap, which is in them. A rollback to before the tick
 * that made a cache gave its block back to the heap and left the class pointing at it: later
 * allocations took the block, and the class's dispose freed it once more at the unload. Windows
 * players crashed so (v0.5.0, 2026-11-14): reading the block's header from a page the heap had
 * given back (0x424b30 called from 0x4f2532, leaving a match to the main menu), and in the heap's
 * free (0x523f1d, 0x523f2b) closing Oni or loading the next level; every such match had Konoko's
 * or Muro's classes in play or the Daodan mutator on.
 * So the cache's block comes from Arklite's slots (memory no snapshot holds), kept by class: a
 * class that asks again (its +0xc30 put back to 0 by a rewind, as under Wine) gets its own slot
 * again, and the class's dispose frees it. The cache's content only depends on the class's body,
 * the same on every PC and in every re-run, and 0x4f2550 fills it again each time it asks. */
#define BODY_CACHE_NEW_CALL     0x4f256d   /* call UUrMemory_Block_New (ECX 0x3a0, ESI the class) */
#define BODY_CACHE_FREE_CALL    0x4f2532   /* call UUrMemory_Block_Delete (ECX the cache, ESI the class) */
#define UUrMemory_Block_New_FN  0x424ab0
#define UUrMemory_Block_Del_FN  0x424b30
#define BODY_CACHE_SIZE         0x3a0
#define BODY_CACHE_SLOTS        512        /* level0 has ~130 classes, a level a few more */
static u8 *g_bc_mem;
static u8 *g_bc_class[BODY_CACHE_SLOTS];   /* the slot's class, 0: free */
static u32 g_bc_made, g_bc_again, g_bc_full_logs;

__attribute__((used, noinline)) static void *__cdecl rb_body_cache_new(u8 *cls)
{
	int free_slot = -1;
	for (u32 i = 0; i < BODY_CACHE_SLOTS; i++) {
		if (g_bc_class[i] == cls) {   /* asked again: its +0xc30 was put back to 0 */
			g_bc_again++;
			return g_bc_mem + i * BODY_CACHE_SIZE;
		}
		if (!g_bc_class[i] && free_slot < 0) free_slot = (int)i;
	}
	if (free_slot < 0) {
		if (g_bc_full_logs++ < 4)
			log_msg("rollback: %d body-surface caches in use: the next one comes from Oni's heap (a rollback past it can break "
			        "the heap)", BODY_CACHE_SLOTS);
		return ((void *(ONI_FC *)(u32))UUrMemory_Block_New_FN)(BODY_CACHE_SIZE);
	}
	g_bc_class[free_slot] = cls;
	g_bc_made++;
	return g_bc_mem + free_slot * BODY_CACHE_SIZE;
}

__attribute__((used, noinline)) static void __cdecl rb_body_cache_free(u8 *cls, u8 *block)
{
	u32 o = (u32)(block - g_bc_mem);
	if (g_bc_mem && block >= g_bc_mem && o < BODY_CACHE_SLOTS * BODY_CACHE_SIZE && !(o % BODY_CACHE_SIZE)) {
		g_bc_class[o / BODY_CACHE_SIZE] = 0;
		return;
	}
	((void (ONI_FC *)(void *))UUrMemory_Block_Del_FN)(block);
}

__attribute__((naked, used)) static void rb_body_cache_new_thunk(void)
{
	__asm__ volatile(
		"pushl %esi\n\t"
		"call _rb_body_cache_new\n\t"
		"addl $4, %esp\n\t"
		"ret");
}

__attribute__((naked, used)) static void rb_body_cache_free_thunk(void)
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %esi\n\t"
		"call _rb_body_cache_free\n\t"
		"addl $8, %esp\n\t"
		"ret");
}

static u32 rb_call_target(u32 site)
{
	const u8 *p = (const u8 *)site;
	return p[0] == 0xe8 ? site + 5 + *(const u32 *)(p + 1) : 0;
}

static void rb_body_cache_install(void)
{
	if (!code_matches(BODY_CACHE_NEW_CALL - 5, (const u8[]){ 0xb9, 0xa0, 0x03, 0x00, 0x00 }, 5) ||
	    rb_call_target(BODY_CACHE_NEW_CALL) != UUrMemory_Block_New_FN || rb_call_target(BODY_CACHE_FREE_CALL) != UUrMemory_Block_Del_FN ||
	    !code_matches(BODY_CACHE_FREE_CALL - 10, (const u8[]){ 0x8b, 0x8e, 0x30, 0x0c, 0x00, 0x00 }, 6) ||
	    !(g_bc_mem = VirtualAlloc(0, BODY_CACHE_SLOTS * BODY_CACHE_SIZE, 0x3000, 4))) {
		log_msg("rollback: the body-surface cache's calls (0x%x, 0x%x) aren't as expected: a rollback past the Daodan glow's "
		        "first tick may break Oni's heap on Windows", BODY_CACHE_NEW_CALL, BODY_CACHE_FREE_CALL);
		return;
	}
	patch_call(BODY_CACHE_NEW_CALL, UUrMemory_Block_New_FN, (u32)rb_body_cache_new_thunk);
	patch_call(BODY_CACHE_FREE_CALL, UUrMemory_Block_Del_FN, (u32)rb_body_cache_free_thunk);
	log_msg("rollback: characters' body-surface caches (the Daodan glow's) come from Arklite's memory: a rollback never takes "
	        "them from a class");
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
	void *p, *q;
	if (h == ONgCrtHeap && between_ticks() && (p = side_alloc(flags, n))) return p;
	p = pool_alloc(h, flags, n, 0);
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
	if (side_has(old)) {   /* one of Arklite's heap (made between ticks) */
		void *p = g_heap_realloc(g_side, flags, old, n);
		if (p && p != old) {
			side_remove(old);
			if (!side_add(p)) log_msg("rollback: a block made between ticks moved and its set is full");
		}
		return p;
	}
	if (slot < 0 && h == ONgCrtHeap && between_ticks() && !(flags & HEAP_REALLOC_IN_PLACE_ONLY)) {
		/* a block of Oni's heap resized between ticks: moved to Arklite's (the old one's freeing
		 * may come back with a rollback: a leak, never a clash) */
		u32 had = g_heap_size(h, 0, old);
		void *p = side_alloc(flags, n);
		if (p) {
			memcpy(p, old, had < n ? had : n);
			g_heap_free(h, 0, old);
			return p;
		}
	}
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
	rb_layout_change();
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
	if (side_has(p)) return g_heap_size(g_side, 0, p);
	return slot >= 0 ? g_pool_size[slot] : g_heap_size(h, flags, p);
}

static int WINAPI rb_heap_free(HANDLE h, u32 flags, void *p)
{
	int slot = pool_slot(p);
	if (side_has(p)) {
		Engine *e = g_e;
		if (e && e->active && e->in_body) {   /* inside a tick: kept (see "Oni's memory between ticks") */
			if (g_side_kept++ < 4) log_msg("rollback: tick %d freed a block made between ticks: kept", e->last_tick);
			return 1;
		}
		side_remove(p);
		return g_heap_free(g_side, 0, p);
	}
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
static int rb_ours(const MEMORY_BASIC_INFORMATION *m, u32 base)
{
	return m->State != 0x10000 /* MEM_FREE */ && (u32)m->AllocationBase == base;
}

/* Still the allocation at base with that span: rb_alloc_alive and rb_span's answer, without the
 * walk over every run of pages between (each a VirtualQuery, 4-8 us on Windows). An allocation is
 * one reservation, its pages one after another, so its span is span exactly when its first and
 * last pages are its own and the page past them isn't. */
static int rb_alloc_same(u32 base, u32 span)
{
	MEMORY_BASIC_INFORMATION m;
	u32 end = base + span;
	if (VirtualQuery((const void *)base, &m, sizeof m) != sizeof m || !rb_ours(&m, base)) return 0;
	if (!span || end < base || end >= 0x7ff00000u) return rb_span(base) == span;   /* (as the walk stops there) */
	if ((u32)m.BaseAddress + m.RegionSize < end &&
	    (VirtualQuery((const void *)(end - 0x1000), &m, sizeof m) != sizeof m || !rb_ours(&m, base)))
		return 0;
	return VirtualQuery((const void *)end, &m, sizeof m) != sizeof m || !rb_ours(&m, base);
}

static void rb_check_allocs(void)
{
	Engine *e = g_e;
	for (u32 k = e->noni; k-- > 0; ) {
		u32 base = e->oni[k], span;
		if (rb_alloc_same(base, e->oni_span[k])) continue;
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
	if (g_heap_alloc && g_heap_realloc && g_heap_free && g_heap_size) rb_side_install();
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

	rb_layout_change();
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
		if (!dropped) rb_layout_change();
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

/* ---- Files Oni read into its heap ------------------------------------------------------- *
 * BFrFile_Map (0x402440) maps a file (the level files: every level's .dat and .raw, mapped at
 * startup and kept). Mapped memory is never a candidate (rb_candidate, rb_grow: MEM_MAPPED). But
 * when the file can't be opened for mapping, because another Oni in the same folder has it open
 * (Oni opens it read/write sharing only reading: the lab's 2-4 Onis, or a player beside their own
 * dedicated server), it reads the whole file into a heap block instead (BFrFile_MapClassic). Those
 * blocks were candidates: 280 MB more than the PC that maps them (a Wine joiner, 2026-11-15),
 * protected and opened again every tick (1.3 ms more a tick), and the same data a rollback puts
 * back on one PC and leaves alone on the other. So they leave the snapshots as a mapping would:
 * every page wholly inside one (rb_file_blocks_out), from the engine's start; a block freed
 * (BFrFile_UnMap 0x4027b0) gives its pages back (rb_grow takes them if Oni's memory is there again). */
#define BFcMapClassicCall    0x40250e   /* in BFrFile_Map: call BFrFile_MapClassic (ECX ref, EDX offset; &file, &ptr, &size) */
#define BFrFile_MapClassic   0x401620
#define BFcUnMapBlockDelCall 0x402802   /* in BFrFile_UnMap, a block's branch: call UUrMemory_Block_Delete (ECX the block) */
#define RB_MAX_FILE_BLOCKS   256
static u32 g_fb_lo[RB_MAX_FILE_BLOCKS], g_fb_hi[RB_MAX_FILE_BLOCKS], g_nfb, g_fb_full, g_fb_seen, g_fb_bytes;

static void rb_file_block_pages(u32 lo, u32 hi, u32 *first, u32 *end)
{
	*first = (lo + 0xfff) >> PAGE_SHIFT;   /* (pages shared with other blocks stay) */
	*end = hi >> PAGE_SHIFT;
}

/* Every page wholly inside a file block: out of the snapshots (as rb_heap_range drops another heap's) */
static void rb_file_blocks_out(void)
{
	Engine *e = g_e;
	u32 dropped = 0, known = 0;
	for (u32 i = 0; i < g_nfb; i++) {
		u32 p, end;
		rb_file_block_pages(g_fb_lo[i], g_fb_hi[i], &p, &end);
		for (; p < end && p < PAGES; p++) {
			if (!(e->flag[p] & PF_CAND)) continue;
			if (!dropped) rb_layout_change();
			if (e->index[p]) known++;
			e->flag[p] = PF_FOREIGN;   /* not a candidate, never protected */
			dropped++;
		}
	}
	if (dropped && e->file_logs++ < 4)
		log_msg("rollback: %d KB of files Oni read into its heap (%d of them; it maps them when no other Oni in this folder "
		        "has them open) left out of the snapshots, as mapped files are (%d of those pages already known); %u files, "
		        "%u MB read so since Oni started", dropped * 4, g_nfb, known, g_fb_seen, g_fb_bytes >> 20);
}

static u32 ONI_FC rb_map_classic(void *ref, u32 offset, void **file, void **ptr, u32 *size)
{
	u32 err = ((u32 (ONI_FC *)(void *, u32, void **, void **, u32 *))BFrFile_MapClassic)(ref, offset, file, ptr, size);
	if ((u16)err || !*ptr || !*size) return err;
	if (!g_fb_seen++)   /* (at startup: a report then shows it even with no match played) */
		log_msg("rollback: Oni reads its game files into its heap instead of mapping them (another Oni in this folder has "
		        "them open; the first is %u KB): more memory and a slower start", *size / 1024);
	g_fb_bytes += *size;
	if (g_nfb < RB_MAX_FILE_BLOCKS) {
		g_fb_lo[g_nfb] = (u32)*ptr;
		g_fb_hi[g_nfb] = (u32)*ptr + *size;
		g_nfb++;
		if (g_e && g_e->active) rb_file_blocks_out();   /* (a file read during a match) */
	} else if (!g_fb_full++) {
		log_msg("rollback: more than %d files read into Oni's heap: the rest stay in the snapshots", RB_MAX_FILE_BLOCKS);
	}
	return err;
}

static void ONI_FC rb_unmap_block(void *block)
{
	Engine *e = g_e;
	for (u32 i = 0; i < g_nfb; i++) {
		if (g_fb_lo[i] != (u32)block) continue;
		if (e && e->active) {   /* its pages, Oni's memory again once used: rb_grow may take them */
			u32 p, end;
			rb_layout_change();
			rb_file_block_pages(g_fb_lo[i], g_fb_hi[i], &p, &end);
			for (; p < end && p < PAGES; p++)
				if (e->flag[p] == PF_FOREIGN) e->flag[p] = 0;
		}
		g_nfb--;
		g_fb_lo[i] = g_fb_lo[g_nfb];
		g_fb_hi[i] = g_fb_hi[g_nfb];
		break;
	}
	((void (ONI_FC *)(void *))UUrMemory_Block_Del_FN)(block);
}

static void rb_file_blocks_install(void)
{
	if (patch_call(BFcMapClassicCall, BFrFile_MapClassic, (u32)rb_map_classic) &&
	    !patch_call(BFcUnMapBlockDelCall, UUrMemory_Block_Del_FN, (u32)rb_unmap_block)) {
		u32 back = BFrFile_MapClassic;   /* without the free, a block's pages would stay out after it's freed */
		patch_call(BFcMapClassicCall, (u32)rb_map_classic, back);
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
	rb_lock_mask(p, (const u32 *)want);
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
	lockstep_glass_rewound();
	rb_os_handles_repair(back);
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
	pw_forget();
	for (u32 i = 0; i < e->npages; i++) {
		u32 p = e->pages[i];
		if (!rb_kept(p)) continue;
		if (!rb_page_differs(p, slot_page(now_slot, i))) continue;
		bad++;
		if (shown++ < 24) rb_out_diff(p, slot_page(now_slot, i));
	}
	if (!bad) out("    none: every known page came out as the first time");
	rb_restore(now_slot);   /* this tick's start as it was, either way */
	lockstep_glass_rewound();

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

/* Where a tick's time goes, around and in its body (the 30 s log line, rollback_phase_text): the checks
 * before (allocations; heaps, particles, OS table), the snapshot copy, protecting, the body, unprotecting,
 * the shared pages' masks, watching (at the frame's last tick: rb_watch); in performance-counter units */
enum { PH_ALLOCS, PH_BEFORE, PH_COPY, PH_PROTECT, PH_BODY, PH_UNPROTECT, PH_SHARED, PH_WATCH, PH_N };
static unsigned long long g_ph[PH_N];
static u32 g_phq[PH_N];   /* and the VirtualQuery calls in each */
static u32 g_ph_ticks;
/* a rollback's restore, the same way: unwatch, allocations, sound, heap journal, pages, handles, orphans */
enum { RS_UNWATCH, RS_ALLOCS, RS_SOUND, RS_HEAP, RS_PAGES, RS_HANDLES, RS_ORPHANS, RS_N };
static unsigned long long g_rs[RS_N];
static u32 g_rsq[RS_N];
static u32 g_rs_n, g_rs_pages;
__declspec(dllimport) int WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int WINAPI QueryPerformanceFrequency(unsigned long long *);
static unsigned long long ph_now(void) { unsigned long long t; QueryPerformanceCounter(&t); return t; }

/* "per tick: before X, copy X, ... us" over the ticks since the last call (then starts again) */
void rollback_phase_text(char *out, u32 size)
{
	static unsigned long long freq;
	static const char *const names[PH_N] = { "allocs", "checks", "copy", "protect", "body", "unprotect", "shared", "watch" };
	u32 n = 0;
	out[0] = 0;
	if (!freq) QueryPerformanceFrequency(&freq);
	if (!g_ph_ticks || !freq) return;
	for (int i = 0; i < PH_N; i++) {
		char one[40];
		u32 us = (u32)((double)(long long)g_ph[i] * 1000000.0 / (double)(long long)freq / (double)g_ph_ticks);   /* (no 64-bit division: -nostdlib) */
		fmt(one, "%s%s %u", i ? ", " : "", names[i], us);
		if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
	}
	{
		char one[192];
		fmt(one, " us a tick over %u ticks (%u runs, %u ticks with the last one's protection kept, %u pages opened between; "
		    "VirtualQuery calls a tick:", g_ph_ticks, g_e ? g_e->last_runs : 0, g_e ? g_e->gap_ticks : 0, g_e ? g_e->gap_faults : 0);
		if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
		for (int i = 0; i < PH_N; i++) {
			fmt(one, " %u%s", g_phq[i] / g_ph_ticks, i + 1 < PH_N ? "" : ")");
			if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
		}
	}
	if (g_rs_n) {
		static const char *const rnames[RS_N] = { "unwatch", "allocs", "sound", "heap", "pages", "handles", "orphans" };
		if (n + 24 < size) { str_cpy(out + n, size - n, "; a restore: "); n += str_len("; a restore: "); }
		for (int i = 0; i < RS_N; i++) {
			char one[40];
			fmt(one, "%s%s %u", i ? ", " : "", rnames[i], (u32)((double)(long long)g_rs[i] * 1000000.0 / (double)(long long)freq / (double)g_rs_n));
			if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
		}
		{
			char one[96];
			fmt(one, " us over %u restores, %u pages put back each (VirtualQuery calls:", g_rs_n, g_rs_pages / g_rs_n);
			if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
			for (int i = 0; i < RS_N; i++) {
				fmt(one, " %u%s", g_rsq[i] / g_rs_n, i + 1 < RS_N ? "" : ")");
				if (n + str_len(one) + 1 < size) { str_cpy(out + n, size - n, one); n += str_len(one); }
			}
		}
	}
	memset(g_ph, 0, sizeof g_ph);
	memset(g_phq, 0, sizeof g_phq);
	if (g_e) g_e->gap_ticks = g_e->gap_faults = 0;
	memset(g_rsq, 0, sizeof g_rsq);
	g_ph_ticks = 0;
	memset(g_rs, 0, sizeof g_rs);
	g_rs_n = g_rs_pages = 0;
}

/* lockstep.c's tick: the engine around body(action), and the test when it's due */
int rollback_test_tick(void *action, void (*body)(void *))
{
	unsigned long long t0 = ph_now(), t1;
	u32 q0 = g_vq;
	Engine *e = g_e;
	u8 *gs = ONgGameState;
	u32 tick;

	if (!e || !gs) return 0;
	if (!e->samples_wanted && !e->start_now && !e->active) return 0;   /* neither the test nor a match */
	g_held = 0;
	tick = GS_TICK(gs);
	if (e->active) rb_unwatch();
	if (e->gap && tick != e->last_tick + 1) rb_gap_end();   /* (not the update's next tick: never expected) */
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
	t1 = ph_now(); g_ph[PH_ALLOCS] += t1 - t0; t0 = t1; g_phq[PH_ALLOCS] += g_vq - q0; q0 = g_vq;
	{
		u32 grew = e->grow;
		if (grew) rb_grow();
		if (grew || !e->heap_walked || !(e->ticks_protected % 60)) rb_heaps_guard();
	}
	rb_particles_check(tick);
	rb_os_table_known();   /* the entries moved if the array grew */
	t1 = ph_now(); g_ph[PH_BEFORE] += t1 - t0; t0 = t1; g_phq[PH_BEFORE] += g_vq - q0; q0 = g_vq;
	rb_snapshot(tick, action);
	t1 = ph_now(); g_ph[PH_COPY] += t1 - t0; t0 = t1; g_phq[PH_COPY] += g_vq - q0; q0 = g_vq;
	if (e->samples_done < e->samples_wanted && tick >= e->next_sample) {
		e->next_sample = tick + SURVEY_EVERY;
		rb_rewind_test(tick, body);
	}
	if (e->ticks_protected < 3) log_msg("rollback: tick %d: protecting %d known pages' neighbours", tick, e->npages);
	if (e->gap && !e->reprot_over && e->layout == e->gap_layout) {   /* the last tick's protection, still on */
		rb_gap_reprotect();
		e->gap_ticks++;
	} else {
		rb_gap_end();
		rb_protect();
		e->gap_layout = e->layout;
	}
	t1 = ph_now(); g_ph[PH_PROTECT] += t1 - t0; t0 = t1; g_phq[PH_PROTECT] += g_vq - q0; q0 = g_vq;
	e->body_serial++;
	e->amb_lo[e->cur] = SSgAmbientNextId;
	e->in_body = 1;
	body(action);
	e->in_body = 0;
	e->amb_hi[e->cur] = SSgAmbientNextId;
	t1 = ph_now(); g_ph[PH_BODY] += t1 - t0; t0 = t1; g_phq[PH_BODY] += g_vq - q0; q0 = g_vq;
	int last = GS_TICK(gs) + 1 >= GS_SERVER_TIME(gs), frame_end = 0;
	if (g_gap_allowed && !e->samples_wanted && e->layout == e->gap_layout && (!last || g_hold)) {
		if (last) g_held = 1;   /* (a hold: the frame's own ticks take it over) */
		/* more ticks in this update, and the runs still fit */
		e->body_serial++;   /* (the watchdog times the gap as a tick of its own) */
		e->gap = 1;
	} else {
		rb_unprotect();
		frame_end = last && !g_hold;   /* (the frame goes on: drawing, the other threads) */
	}
	t1 = ph_now(); g_ph[PH_UNPROTECT] += t1 - t0; t0 = t1; g_phq[PH_UNPROTECT] += g_vq - q0; q0 = g_vq;
	/* shared pages: what this tick changed (from its start, in its slot), and the end for the
	 * next tick's start to compare with */
	pw_forget();
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
	t1 = ph_now(); g_ph[PH_SHARED] += t1 - t0; t0 = t1; g_phq[PH_SHARED] += g_vq - q0; q0 = g_vq;
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
	if (frame_end) {
		e->frames_ended++;
		rb_watch(e->frames_ended <= RB_WATCH_FIRST ? -1 : (int)(e->frames_ended % RB_WATCH_EVERY));
	}
	g_ph[PH_WATCH] += ph_now() - t0; g_phq[PH_WATCH] += g_vq - q0;
	g_ph_ticks++;
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
		if (!e || !e->active || !(e->in_body || e->gap) || !e->main_thread) {
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
		if (e->in_fault || !(e->in_body || e->gap) || e->body_serial != serial) {   /* moving after all */
			ResumeThread(e->main_thread);
			continue;
		}
		memset(ctx, 0, sizeof ctx);
		ctx[0] = 0x10007;   /* CONTEXT_FULL */
		int have = GetThreadContext(e->main_thread, ctx);
		u32 learned = 0;
		stage++;
		if (!e->in_body) stage = 2;   /* stuck between two ticks with the protection kept: open it all */
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
			e->layout++;   /* (the next tick protects in full) */
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

/* The sounds Oni's ambient table starts between ticks. Its entries are game memory, but it
 * starts and halts SS2 plays outside the ticks too: each frame's sound update
 * (OSiAmbient_UpdateAll, from the frame after its ticks) starts a play when the sound comes into
 * hearing range (0x512062 in OSiAmbient_Update) and halts it when it leaves. A play started there
 * after a tick's snapshot was lost when a rollback put that tick back: the entry got its old
 * "not playing" back, the next frame started a second play, and the first looped forever (no
 * id range of the ticks has it; user report 2026-11-12: Screaming Cells heard all the time
 * during a match, more often with Fast Screamer's cells crossing in and out of range).
 * So the plays the table starts (both its calls of SSrAmbient_Start: 0x511973 in
 * OSrAmbient_Start, 0x512062) are noted, and after a restore any of them still playing that no
 * entry holds is stopped. The table plays them again if they still should. */
static const u32 g_os_start_calls[] = { 0x511973, 0x512062 };
static u32 g_os_started[1024];   /* the table's play ids, at [id & 1023] */
static u32 (ONI_FC *g_ss_ambient_start)(void *, void *, u32, u32, u32, u32, u32) =
	(u32 (ONI_FC *)(void *, void *, u32, u32, u32, u32, u32))SSrAmbient_Start;

static u32 ONI_FC os_ambient_start(void *ambient, void *pos, u32 dir, u32 vel, u32 maxd, u32 mind, u32 vol)
{
	u32 id = g_ss_ambient_start(ambient, pos, dir, vel, maxd, mind, vol);
	if (id != 0xffffffffu) g_os_started[id & 1023] = id;
	return id;
}

static void rb_os_orphans(u32 tick)
{
	static u32 held[OScPlayingHandles];
	static int logs;
	Engine *e = g_e;
	u32 *t = SSgAmbients, n = SSgAmbientCount, nheld = 0;
	u8 **h = OSgPlayingHandles;

	if (!t || n > 4096 || !rb_readable((u32)t) || !rb_readable((u32)t + n * 0x58 - 1)) return;
	if (!h || !rb_readable((u32)h) || !rb_readable((u32)h + OScPlayingHandles * 4 - 1)) return;
	for (u32 i = 0; i < OScPlayingHandles; i++) {
		const u8 *a = h[i];
		if (a && rb_readable((u32)a) && rb_readable((u32)a + 0x4b) && OS_PLAY_ID(a) != 0xffffffffu)
			held[nheld++] = OS_PLAY_ID(a);
	}
	for (u32 i = 0; i < n; i++) {
		u32 *a = t + i * 0x16, id = a[0], k = 0;
		if (!a[1] || a[1] == SS_STATE_STOPPING || g_os_started[id & 1023] != id) continue;
		while (k < nheld && held[k] != id) k++;
		if (k < nheld) continue;
		if (logs++ < 8) {
			const char *name = a[2] && rb_readable(a[2]) && rb_readable(a[2] + 31) ? (const char *)a[2] : "?";
			log_msg("rollback: tick %d: stopped %.32s (play %u), which Oni's ambient table started and no longer holds",
			        tick, name, id);
		}
		SSrAmbient_Stop(id);
		g_os_started[id & 1023] = 0;
		e->amb_stopped++;
	}
}

/* The ambient table as one. Its array (*0x61fa2c, UUtMemory_Array: +4 used, +0xc allocated, +0x10
 * data, 0x4c-byte entries: +0 index, +4 handle, +0xc SS2 play id) and its handles (*0x61fa34, 1024)
 * change in ticks (a start: a new entry and handle) and between them (the frame's
 * OSiAmbient_UpdateAll frees each entry whose sound ended: the count goes down, the entries move
 * up, every handle is made again). Oni keeps them matching. v0.5.0, 2026-11-14: three Windows
 * joiners crashed a second after coming into a match in progress (PowerPlant, a modded level0),
 * each with the same registers: OSrAmbient_Stop's delete (0x425570) ran with the array's count at
 * 0 (memmove's end, src + n, was the data's start: n = -(index + 1) * 0x4c), so it ran off the
 * array (0x51f8f3): a handle led to an entry of an empty array. Before each crash: catching up
 * (the sound driver off, so every PlayOnce line in hearing range stays in the table, never
 * played), coming in (the first frames started them all: c18_79_05tanker eight times), and
 * restores (rb_os_orphans stopped those plays). Only a restore that put back one part of the table
 * and not the other can part them; which part wasn't shown, and 2400 rollbacks of a hidden test
 * (Wine) didn't reproduce it. One way the code allows: a page is learned when a tick first writes
 * it, and rb_learn copies it as it is then into every kept snapshot, the frames' writes since those
 * ticks included. So: the table's three blocks are known pages from the snapshots' start (and
 * checked at every tick's start: the data block moves when the array grows), and after every
 * restore the handles are made again from the array if they don't match (rb_os_handles_repair). */
#define OSgPlayingArray      (*(u32 **)0x61fa2c)
#define OScPlayingEntrySize  0x4c

static void rb_learn_range(u32 lo, u32 n)
{
	Engine *e = g_e;
	for (u32 p = lo >> PAGE_SHIFT; n && p <= (lo + n - 1) >> PAGE_SHIFT && p < PAGES; p++) {
		if (!(e->flag[p] & PF_CAND) || e->index[p] || (e->flag[p] & PF_FOREIGN)) continue;
		if ((e->flag[p] & PF_PROT) && !e->gap) continue;
		if (e->npages >= RB_MAX_PAGES) return;
		rb_lock();
		if (e->flag[p] & PF_PROT) rb_gap_open_locked(p);   /* (the kept protection: as if it were off) */
		if (!(e->flag[p] & PF_PROT)) rb_learn(p);
		rb_unlock();
		e->os_learned++;
	}
}

static void rb_os_table_known(void)
{
	const u32 *a = OSgPlayingArray;
	u8 **h = OSgPlayingHandles;
	if (a && rb_readable((u32)a) && rb_readable((u32)a + 0x13)) {
		rb_learn_range((u32)a, 0x14);
		if (a[4] && a[0] == OScPlayingEntrySize && a[3] && a[3] <= OScPlayingHandles && rb_readable(a[4]))
			rb_learn_range(a[4], a[0] * a[3]);
	}
	if (h && rb_readable((u32)h)) rb_learn_range((u32)h, OScPlayingHandles * 4);
}

/* After a restore: the handles made again from the array, as Oni does after every change
 * (OSiPlayingAmbientHandles_Update, inline after 0x425570's calls: each entry gets its index at
 * +0 and the handle at +4 points at it). Oni keeps the two matching; only a restore that put
 * back one part and not the other parts them, and the crash above needed handles to entries
 * past the array's count. Logged when they didn't match. */
static void rb_os_handles_repair(u32 tick)
{
	static u32 logs;
	u32 *a = OSgPlayingArray;
	u8 **h = OSgPlayingHandles;
	if (!a || !h || !rb_readable((u32)a) || !rb_readable((u32)a + 0x13) || !rb_readable((u32)h) ||
	    !rb_readable((u32)h + OScPlayingHandles * 4 - 1))
		return;
	u32 n = a[1], data = a[4], bad = 0;
	if (a[0] != OScPlayingEntrySize || n > a[3] || n > OScPlayingHandles || (n && (!data || !rb_readable(data) ||
	    !rb_readable(data + n * OScPlayingEntrySize - 1)))) {
		if (logs++ < 4) log_msg("rollback: tick %d: Oni's ambient array doesn't look right after the restore (entry size %u, count %u of %u): "
		                        "left as it is", tick, a[0], a[1], a[3]);
		return;
	}
	for (u32 i = 0; i < OScPlayingHandles; i++) {   /* a handle to anything but its entry in the array */
		u32 p = (u32)h[i];
		if (!p) continue;
		if (p < data || p >= data + n * OScPlayingEntrySize || (p - data) % OScPlayingEntrySize || ((u32 *)p)[1] != i) bad++;
	}
	for (u32 i = 0; i < n; i++) {   /* an entry no handle points at */
		u32 *ent = (u32 *)(data + i * OScPlayingEntrySize);
		if (ent[1] >= OScPlayingHandles || h[ent[1]] != (u8 *)ent || ent[0] != i) bad++;
	}
	if (!bad) return;
	e_os_repairs++;
	if (logs++ < 8) log_msg("rollback: tick %d: Oni's ambient handles didn't match its array after the restore (%u differences, %u "
	                        "entries): made again from the array", tick, bad, n);
	memset(h, 0, OScPlayingHandles * 4);
	for (u32 i = 0; i < n; i++) {
		u32 *ent = (u32 *)(data + i * OScPlayingEntrySize);
		ent[0] = i;
		if (ent[1] < OScPlayingHandles && !h[ent[1]]) h[ent[1]] = (u8 *)ent;
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
	unsigned long long r0 = ph_now(), r1;
	u32 v0 = g_vq;
	rb_gap_end();   /* (between updates: never on here) */
	rb_unwatch();
	r1 = ph_now(); g_rs[RS_UNWATCH] += r1 - r0; r0 = r1; g_rsq[RS_UNWATCH] += g_vq - v0; v0 = g_vq;
	rb_check_allocs();   /* memory another thread freed since this tick began: not put back */
	r1 = ph_now(); g_rs[RS_ALLOCS] += r1 - r0; r0 = r1; g_rsq[RS_ALLOCS] += g_vq - v0; v0 = g_vq;
	/* the sound thread waits until the restore is done: it reads memory being put back */
	u8 *lock = SSgLock;
	if (lock) SSrLock(lock);
	rb_sound_before_restore(tick);
	r1 = ph_now(); g_rs[RS_SOUND] += r1 - r0; r0 = r1; g_rsq[RS_SOUND] += g_vq - v0; v0 = g_vq;
	heap_check_rewound(GS_TICK(ONgGameState), tick);   /* (its journal: blocks this rewind makes free again) */
	r1 = ph_now(); g_rs[RS_HEAP] += r1 - r0; r0 = r1; g_rsq[RS_HEAP] += g_vq - v0; v0 = g_vq;
	rb_restore(s);
	lockstep_glass_rewound();   /* panes broken in the ticks taken back: whole again (lockstep.c "Broken glass") */
	r1 = ph_now(); g_rs[RS_PAGES] += r1 - r0; r0 = r1; g_rsq[RS_PAGES] += g_vq - v0; v0 = g_vq;
	rb_os_handles_repair(tick);   /* (before the orphans: they read the handles) */
	r1 = ph_now(); g_rs[RS_HANDLES] += r1 - r0; r0 = r1; g_rsq[RS_HANDLES] += g_vq - v0; v0 = g_vq;
	rb_os_orphans(tick);
	r1 = ph_now(); g_rs[RS_ORPHANS] += r1 - r0; g_rsq[RS_ORPHANS] += g_vq - v0;
	g_rs_n++;
	if (lock) SSrUnlock(lock);
	GS_TIME_BASE(ONgGameState) = base;
	e->last_tick = tick;   /* not "the level restarted" */
	return 1;
}

/* report.c: this run's rollback check file, "" if none */
const char *rollback_check_path(void) { return g_vpath; }

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
	rb_gap_end();
	rb_unwatch();
	rb_rd_forget();
	pw_forget();
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
		str_cpy(g_vpath, sizeof g_vpath, path);
		g_out = e->vout;
		out("Arklite rollback check: a rollback's ticks run once more with the same inputs; the two runs compared.");
		out("A character that differs is game state a rollback leaves behind. 'first time' = the rollback's run,");
		out("'re-run' = the check's run. Expected page noise: ambient sounds' play ids.");
	}
	g_out = e->vout;
	out("");
	out("check %d: %s", e->vchecks, what);
	game_bad = rb_game_compare();
	pw_forget();
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
	u32 calls = 0;

	rb_body_cache_install();
	rb_file_blocks_install();
	for (u32 i = 0; i < sizeof g_os_start_calls / sizeof g_os_start_calls[0]; i++)
		calls += patch_call(g_os_start_calls[i], SSrAmbient_Start, (u32)os_ambient_start);
	if (calls == 2) log_msg("rollback: the sounds Oni's ambient table starts are noted (a rollback stops the ones it loses)");
	else log_msg("rollback: %u of Oni's ambient table's 2 sound starts found; lost ones may loop after rollbacks", calls);

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
